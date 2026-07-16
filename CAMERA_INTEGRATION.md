# Camera streaming -- round 3

All five DCMIPP values from last round's table are fixed at the source now
(checked against `RedEye.ioc`: `NumberOfLanesPIPE1`, `DataTypeIDAPIPE1`/
`IDBPIPE1`, `DCMIPP_CSI_DATA_TYPE_FORMAT1`, `PixelPipePitchPIPE1` are all
correct), and DCMIPP also picked up a complete native RIF grant
(`RIF.RISUP.DCMIPP.Privilege=true` in the ioc -- however you found that
panel, it worked, and now both DCMIPP and JPEG have real native grants
instead of needing a hand-patched one). Good progress -- most of what's
left in this doc from here is just the PHY bitrate caveat, unchanged.

**One real bug, not just cleanup:** my old override from last round was
re-setting DCMIPP's slave/RISC attribute to `NPRIV` *after* the
newly-correct auto-generated code had just set it to `PRIV` -- silently
undoing the fix you'd just made, every single build. Removed. If DCMIPP
still isn't behaving, that's worth knowing about even though it's fixed
now: the override was masking the real (correct) value the whole time you
were probably testing against it.

Also simplified `USER CODE BEGIN DCMIPP_Init 2` down to just
`PHYBitrate` -- the other four fields it was re-asserting are redundant
now that they're correct at the source, and leaving them in would hide it
if CubeMX ever regenerates a wrong value again.

## Still the same one open question

`DCMIPP_CSI_PHY_BT_400` is still a guess -- nothing in this round changed
it, and it's the only value left in the whole pipeline I haven't been able
to verify against your actual module. If the sensor probes fine (I2C2
ACKs, `OV5647_ReadID` returns `0x5647`) but no image ever shows up --
`HAL_DCMIPP_PIPE_FrameEventCallback` never fires -- that's the thing to
try adjacent values on. Everything else in this doc from the last two
rounds (garbled image, wrong colors, dark/bright, freezes after one
frame, laggy) is unchanged.

Once it's running: `http://<board-ip>/` in a browser, or `ffplay
http://<board-ip>/`.
