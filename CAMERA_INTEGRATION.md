# Camera streaming -- round 2

Good progress on the CubeMX side since last round: **NVIC for DCMIPP_IRQn/
JPEG_IRQn is now properly enabled, the IRQ handlers are natively
generated, and DCMIPP is CSI-2 mode now instead of parallel.** Those were
three separate things I'd patched by hand last time -- all now fixed
properly at the source, so I didn't need to touch them this round. JPEG
also now has its own native RIF grant (`RIF_ATTRIBUTE_SEC |
RIF_ATTRIBUTE_PRIV`, in the auto-generated "RISUP configuration" section) --
I don't know exactly how you got CubeMX to emit that given it isn't in
`RedEye.ioc`'s `RIF.IPParameters` list, but empirically it's there now, so
whatever you did, it worked.

Two things left:

## 1. DCMIPP's CSI config has the wrong values for OV5647

CubeMX generated real CSI-2 config this time (progress!), but with values
that don't match this sensor:

| Field | Generated | Should be | Why |
|---|---|---|---|
| `NumberOfLanes` | `DCMIPP_CSI_ONE_DATA_LANE` | `DCMIPP_CSI_TWO_DATA_LANES` | CN6 breaks out 2 data lanes (CSI_D0, CSI_D1) |
| `DataTypeIDA`/`IDB` | `DCMIPP_DT_YUV420_8` | `DCMIPP_DT_RAW8` | OV5647 has no on-sensor ISP -- it only ever outputs raw Bayer, never YUV |
| VC bits/pixel | `DCMIPP_CSI_DT_BPP6` | `DCMIPP_CSI_DT_BPP8` | matches RAW8 |
| `PHYBitrate` | `DCMIPP_CSI_PHY_BT_80` | `DCMIPP_CSI_PHY_BT_400` (estimate) | see caveat below |
| `PixelPipePitch` | `10` | `1280` | 640px * 2 bytes/px for the RGB565 pipe output |

Fixed in `USER CODE BEGIN DCMIPP_Init 2` (survives regeneration), but the
durable fix is those same five fields in CubeMX's DCMIPP GUI panel --
worth doing next time you're in there so the override stops being
necessary.

**Caveat, same as always:** the PHY bitrate is a guess. It needs to match
whatever MIPI clock the OV5647 actually ends up running at with the
register table in `ov5647.c`, which I still haven't been able to verify
against your module. If the sensor probes fine (I2C2 ACKs, `OV5647_ReadID`
returns `0x5647`) but `HAL_DCMIPP_PIPE_FrameEventCallback` never fires --
no image, nothing -- this is the first thing to try adjacent values on.

## 2. DCMIPP's RIF grant is still incomplete

JPEG got its native fix; DCMIPP didn't. It still only has the
auto-generated master (RIMU) grant at `NPRIV`, same issue ETH1's master
had, and no slave (RISC) grant at all. Added to `USER CODE BEGIN
RIF_Init 1` (extending the block that already had the ETH1 fix from
earlier sessions), mirroring that same proven pattern: master upgraded to
`PRIV`, plus a slave grant it never had at `NPRIV`.

One honest asterisk: JPEG's new native grant uses `PRIV` for its (only)
slave/RISC attribute, not `NPRIV` like ETH1's proven one. I don't have a
clean way to know for certain whether DCMIPP wants `NPRIV` (matching
ETH1, another DMA-bus-mastering peripheral, which is the closer structural
match) or `PRIV` (matching JPEG, which is the more recent, tool-verified
data point) without testing. Went with `NPRIV` as the closer analogy --
if DCMIPP still doesn't come up, flipping that one line to `PRIV` is the
next thing to try.

## Also wired up this round

`ov5647.c/h`, `camera_stream.c/h`, `mjpeg_server.c/h` -- these were never
carried over into whatever CubeMX project you've been regenerating from
(makes sense, they're not something CubeMX would know to keep). Re-added
directly to `Appli/Src`/`Appli/Inc`, plus the same `CAMERA_STREAM_Init()`/
`MJPEG_SERVER_Init(80)` calls in `USER CODE 2` and `CAMERA_STREAM_Process()`/
`MJPEG_SERVER_Poll()` in the main loop as last time.

Once it's running: `http://<board-ip>/` in a browser, or `ffplay
http://<board-ip>/`. Full debugging checklist (garbled image, wrong
colors, dark/bright, freezes after one frame, laggy) is unchanged from
the first pass -- ask if you don't have it handy anymore.
