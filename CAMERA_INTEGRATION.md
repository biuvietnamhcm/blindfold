# Camera streaming -- round 8

Regenerated from CubeMX after setting CSI2HOST to Secure+Privileged in
the RIF panel (round 7's ask). Confirmed the grant is now native
(`RIF.RISUP.CSI2HOST.Privilege=true` in the ioc, and
`HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI, ...)`
generated natively in `SystemIsolation_Config()`) -- the hand-added
duplicate grant was retired the same way DCMIPP's was.

**But the same regeneration silently broke four other things, worth
understanding since it'll happen again:**

1. **`stm32n6xx_hal_dcmipp.c` (`Drivers/`) reverted to the buggy `0xe3`.**
   Earlier rounds assumed `Drivers/` is copied wholesale and never
   touched by "Generate Code" -- empirically wrong, at least for
   whatever regeneration mode was used here. Re-patched; **this now
   needs reapplying after every regeneration** until ST ships a fix
   upstream (worth reporting, if you haven't).
2. **IC18's divider reverted to 1** (1600 MHz again) in the
   auto-generated part of `HAL_DCMIPP_MspInit()` -- expected, this was
   flagged as a live risk since round 6 (RedEye.ioc's Clock
   Configuration still doesn't know about the /80 fix). The USER CODE
   re-assert right after it caught this correctly, so the *net* clock is
   still right -- but this is the second time this exact warning has
   proven itself; worth actually setting IC18 to PLL4/80 in CubeMX's
   Clock Configuration tab now, the same way CSI2HOST just got fixed
   properly instead of staying defensive.
3. **`_Min_Stack_Size` reverted to CubeMX's 2 KB default** (was 16 KB) in
   the linker script. This one's a real hazard, not just a reversion of
   something already re-asserted elsewhere -- there's no USER CODE
   equivalent for a linker script, so a stack overflow risk sat
   unprotected. Restored to 0x4000.
4. **`CSI_IRQHandler()` disappeared entirely from `stm32n6xx_it.c`** --
   not reverted, *deleted*. This is a different failure mode from (1)/
   (2): CSI_IRQn's NVIC enable only ever lived in a USER CODE section in
   msp.c (defensively, precisely so regeneration couldn't touch it), but
   that means CubeMX's own cross-file model of "which IRQs exist" never
   learned about CSI_IRQn, so when it regenerated `stm32n6xx_it.c` it had
   no reason to keep (or re-generate) a handler for it. Since CSI_IRQn
   fires constantly (that's the whole "Er" counter), this would have
   hung the board on the very first CSI interrupt after boot -- a worse
   failure than anything the actual camera bug has produced so far, and
   it would have looked like the RIF fix "broke" something instead of an
   unrelated regeneration casualty. Restored. **This is the strongest
   argument yet for actually enabling CSI_IRQn through CubeMX's NVIC tab**
   instead of leaving it defensive -- a value silently reverting is
   recoverable by the pattern this project already uses everywhere; a
   whole function silently vanishing is not, and there's no way to
   defend against that class of loss from inside the file it deletes.

Also found and restored: `BlinkBlue()` (blink-coded camera error
indicator, LED_BLUE blinked 2/3/4/5 times for NODEV/INIT/DCMIPP/FAIL) had
its definition deleted while its declaration and 4 call sites survived --
an undefined-reference build break. It was sitting unprotected between
`MX_xxx_Init()` functions; moved into `USER CODE BEGIN 4` alongside
`Netif_Config()` so this doesn't happen again.

**Net lesson:** `Drivers/`, linker scripts, and cross-file dependencies
(an IRQ enabled in one file needing a handler in another) are all
regeneration-fragile in ways plain USER CODE markers in a single file
don't fully protect against. Diffing the whole tree against the last
known-good copy after every regeneration -- not just spot-checking the
files you expect to have changed -- is the only reliable way to catch
this class of damage before it costs a confusing debugging session.

**Also added, as requested:** `CAM_CSI_AUTO_SCAN_ENABLE` in
`camera_stream.h`, defaulting to **0 (manual)**. With it off,
`CAMERA_STREAM_Init()` applies `CAM_CSI_PHY_BITRATE`/`CAM_CSI_LANE_MAPPING`
once and stops -- no scanning, plain v/c/e/Er telemetry on the OLED,
exactly the round-4 manual-bracketing workflow. Flip it to 1 to get
round 5's 253-combination auto-scan back. Current manual value is still
BT_300/PHYSICAL -- this build is specifically for testing whether the
RIF grant alone gets that one, specific, mathematically-derived
combination to lock, in isolation, with nothing else touching CSI config
at the same time.

---

# Camera streaming -- round 7

Found something that reframes every round before this one: **CSI never had
a RIF security/privilege grant at all.**

`SystemIsolation_Config()` (main.c) explicitly grants DCMIPP and JPEG
`RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV` on their RISC (slave/peripheral)
attributes -- and this project's own history shows *why* that grant
matters: an earlier round found DCMIPP itself sitting at NPRIV by
mistake, with the exact symptom this whole security model produces --
HAL calls return `HAL_OK`, nothing complains, and the actual hardware
transaction just never happens (documented in the ETH1 fix comment right
above this one: "HAL calls report success... the actual DMA bus
transaction... never happens"). That's a fundamentally silent failure
mode -- there's no error code to catch, no exception, nothing to see in
a debugger's return-value trace. It looks exactly like "the config is
wrong," which is what every round so far assumed.

`RIF_RISC_PERIPH_INDEX_CSI` is a real, separate RISC resource --
confirmed in `stm32n6xx_hal_rif.h`: bit SEC28, immediately next to
DCMIPP's SEC29 in the same register. It has never been granted anywhere
-- not in the generated code, not in `RedEye.ioc` (no `RIF.RISUP.CSI.*`
key exists at all, confirmed by grep, vs. DCMIPP's
`RIF.RISUP.DCMIPP.Privilege=true`). It's been sitting at whatever the
silicon reset default is through every one of the last three rounds.

Why this is a stronger explanation than anything found so far: it would
mean every `HAL_DCMIPP_CSI_SetConfig()` call across this entire bring-up
-- the osc_freq_target fix, the IC18 clock fix, all 253 combinations in
the auto-scan -- returned `HAL_OK` while potentially never actually
reaching the CSI peripheral's real registers, fenced by the RIF security
fabric before the write landed. That would explain why a *correct* HAL
fix and an *exhaustive* combination sweep both produced zero change: none
of those writes were guaranteed to be taking effect in silicon in the
first place, regardless of how correct their values were.

**Fixed:** added the matching grant, same pattern as the ETH1 fix right
above it in the same function:
```c
HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
```
in `USER CODE BEGIN RIF_Init 1`, so it's live immediately without needing
a CubeMX regeneration first.

**Still worth doing in CubeMX** (this is the one to actually make native,
if you're regenerating anyway): open `RedEye.ioc`, find the RIF panel's
RISUP/peripheral-attributes table, locate CSI, and grant it Secure +
Privileged -- the same treatment DCMIPP and JPEG already have natively
(`RIF.RISUP.DCMIPP.Privilege=true` etc.). Once that's a real ioc key, the
code-level grant above stops being a defensive workaround and becomes
redundant-but-harmless, matching how DCMIPP's own history played out.
Send the regenerated folder over and I'll verify the grant landed
correctly and that nothing else shifted in the regeneration.

Confidence note: this is the best-evidenced remaining explanation --
real distinct resource, confirmed absent, matches a failure class this
exact project has already hit twice (DCMIPP, ETH1) -- but "best explains
why nothing else worked" isn't the same as "confirmed fixed." Rebuild,
reflash, watch the scan.

---

# Camera streaming -- round 6

Auto-scan (round 5) came back NO LOCK across all 253 combinations. Sensor
+ cable independently confirmed good on a real Raspberry Pi Zero 2 W (a
real MIPI CSI-2 D-PHY receiver, unlike the Pico 2 W first mentioned --
Pico/Pico 2/2W have no D-PHY receiver in RP2040/RP2350 silicon at all, per
an actual Raspberry Pi engineer on their forum, so that first check
wouldn't have proven much either way). Same "hbv raspberry 150 fpc"
15-pin-1mm-to-22-pin-0.5mm cable, no separate adapter board in the path
(the "MB1723" mentioned in earlier rounds' comments was apparently never
actually confirmed to be a real, separate part in this build -- worth
striking from future notes unless it turns up again).

With the sensor+cable now independently validated, went hunting on the
STM32N6 side specifically. Pulled ST's exact 22-pin pin assignment for
this connector (UM3354, Table 3) and cross-referenced it against the
generic Raspberry Pi 15-pin (OV5647's own connector) and generic RPi
22-pin (e.g. Pi 5/CM/Zero 2 W) standards. The reassuring part: pins 1-10
(all three CSI differential pairs -- D0, D1, CLK) and 19-22 (I2C+power)
are in *identical* positions across all three standards; only pins 11-18
differ (ST dedicates those to ToF/IMU signals a plain camera doesn't
have, vs the generic standard's 4-lane support / power-LED-enable) --
and this project already bypasses those entirely, driving power/reset
via GPIOs on CN6 directly rather than trusting generic pins 17/18, so
that mismatch was never live. MCU selection in RedEye.ioc
(STM32N657X0HxQ, VFBGA264) matches the board's actual populated part
(STM32N657X0H3Q) exactly. The 6 CSI D-PHY pins (CKN/CKP/D0N/D0P/D1N/D1P)
are correctly reserved in the ioc's pinout (Mode=CSI, BGA pins 37/38/39/
47/48/49) with no GPIO_Init() anywhere for them, which is correct, not
missing -- they're dedicated analog D-PHY pins outside the GPIO/AF mux
entirely, same pattern as similar Synopsys D-PHY IP on other ST parts.

Did find one real, previously-missed gap, though: **DCMIPP and CSI are
separate reset domains on APB5** (confirmed in stm32n6xx_ll_bus.h --
`LL_APB5_GRP1_PERIPH_CSI` and `_DCMIPP` are distinct bits). This
project's `HAL_DCMIPP_MspInit()` enables DCMIPP's clock and resets CSI
(`__HAL_RCC_CSI_FORCE_RESET`/`RELEASE_RESET`), but never resets DCMIPP
itself -- only ever enables its clock. A working ST community bring-up
example for this exact peripheral (community.st.com, "STM32N6: DCMIPP")
explicitly pairs `__HAL_RCC_DCMIPP_CLK_ENABLE()` with
`__HAL_RCC_DCMIPP_FORCE_RESET()`/`RELEASE_RESET()`; this project never
did. Added it (`stm32n6xx_hal_msp.c`, USER CODE section, so it survives
regeneration same as the IC18 fix). Being honest about confidence here:
CSI's own reset (which is more directly responsible for D-PHY/protocol
state) was already in place, so this is a real gap worth having fixed
regardless, but it's not established with the same certainty as the
osc_freq_target HAL bug that it's THE reason for the lock failure
specifically -- it's "definitely should be there," not "definitely
explains v:0 Er-climbing."

## Next: rebuild, reflash, watch the auto-scan again

If it locks this time -- great, note which combination and hardcode it.
If it's still NO LOCK after all of the above (HAL bug fixed, IC18
correct, exhaustive combination sweep, sensor+cable now independently
proven good on real hardware, DCMIPP reset added, pin mapping verified
against ST's own manual) -- that's a genuinely harder result to explain
by elimination, and the next move is probably a scope/logic analyzer on
CSI_CLK_P/N (Nucleo BGA pins 37/47) if available, since everything
software-checkable has now been checked.

---

# Camera streaming -- round 5

BT_300 (round 4's derived-correct value, calibration bug now fixed) still
didn't lock -- same v:0 c:0, Er climbing. So instead of another manual
guess/rebuild/reflash, `CAMERA_STREAM_Init()`/`CAMERA_STREAM_Process()` in
`camera_stream.c` now do it automatically: if PIPE1 hasn't seen a vsync
~120ms after boot, they walk every `PHYBitrate` (BT_80..BT_2500) x
`DataLaneMapping` (physical/inverted) x `NumberOfLanes` (1/2) combination
the CSI-2 receiver supports -- 253 total -- re-applying
`HAL_DCMIPP_CSI_SetConfig()` and watching the vsync counter, and stop the
moment one produces a real frame start.

This is a non-blocking state machine (`csi_scan_tick()`, driven from
`CAMERA_STREAM_Process()` in the main loop), not a boot-time freeze -- one
new candidate gets tried at most every ~120ms, so a full sweep is ~30s
worst case with the network/OLED/everything else still running normally
throughout. Progress and the result show up on the OLED, rows 48/56 (the
same two rows the v/c/Er telemetry already used -- reused rather than
added, since every other row on this display is already claimed --
see the layout note further up this file):
  - while scanning: `Scan <n>/253` and `T:<mbps> <lanes>L<P|I>` (the
    candidate currently being tried)
  - once it locks: v/c revert to normal telemetry, and row 56 becomes
    `LK:<mbps> <lanes>L<P|I>` -- **that's the combination to copy into
    `CAM_CSI_PHY_BITRATE`/`CAM_CSI_LANE_MAPPING` in `camera_stream.h`**
    so future boots lock on step 0 again instead of re-scanning every
    time (deliberately not persisted to flash -- see below)
  - if all 253 fail: `NO LOCK (chk HW)`, permanently -- at that point
    every PHYBitrate/lane-mapping/lane-count combination the receiver
    supports has been tried without a single vsync, which stops being a
    software config question. Most likely next suspects, roughly in
    order: the sensor isn't actually powered/streaming (check
    `OV5647_ReadID` succeeded, not just that `CAMERA_STREAM_Init()`
    returned OK -- it also returns OK if the scan simply hasn't finished
    yet); the MB1723 adapter isn't passing the clock lane through at all
    (a lane *mapping* swap is covered by this scan, but a clock lane
    that's simply not connected isn't -- worth checking with a scope if
    you have one); or the adapter genuinely isn't the right part (see the
    round-4 note above about not being able to verify "MB1723" as a
    public ST reference).

Deliberately not persisted across power cycles (no flash writes): this is
a bring-up aid, not a runtime feature, and flash has finite write cycles.
Once you see `LK:...`, hardcode it and this scan becomes a one-step no-op
(step 0) forever after -- there's no reason to keep scanning on every
boot once the real answer is known.

---

# Camera streaming -- round 4

**Found the real reason no `PHYBitrate` bracket ever locked the PHY, and it
wasn't in this project.** It's a bug in ST's own HAL, in
`HAL_DCMIPP_CSI_SetConfig()` (`Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c`):
programming the D-PHY's DLL calibration target (`osc_freq_target`) is
supposed to split that value across two Synopsys D-PHY test-interface
registers, 0xe2 (low byte) and 0xe3 (high byte) -- the function's own
comment says as much ("set reg @0xe3 & reg @0xe2"). What the code actually
does is write 0xe3 twice: once with the high byte, then immediately again
with the low byte, which just clobbers the first write. Register 0xe2 is
never touched at all and sits at its power-on-reset value forever.

Confirmed two ways before touching anything:
- Diffed our `Drivers/` copy against `stm32n6xx-hal-driver`'s `main` branch
  on GitHub -- byte-for-byte identical, so this isn't something patched
  locally, it's ST's shipped code, still present today.
- Cross-checked against Synopsys's own `dw-dphy-rx` Linux reference driver
  for the same D-PHY IP (from the `media: platform: dwc` kernel patch
  series) -- it names these exact test-codes `RX_RX_STARTUP_OVR_2`/`_OVR_3`
  and writes the low byte to 0xe2, high byte to 0xe3. Confirms both the
  register split and which byte goes where.

Why this explains the symptom exactly: `osc_freq_target` is the same value
(460) for every `PHYBitrate` band from `BT_80` up to `BT_1500` in ST's
table -- i.e. the entire range worth trying for something like the OV5647
(291.67 Mbit/s/lane). Every bracket in that range shares the identical
broken calibration write, so bracketing `PHYBitrate` could never have
worked no matter which band got picked. Fixed in the HAL file directly
(look for the `FIX` comment next to `DCMIPP_CSI_WritePHYReg`); `main.c`'s
`CAM_CSI_PHY_BITRATE` is back to the derived-correct `BT_300` since
bracketing away from it was only ever working around this bug.

Worth reporting upstream (community.st.com or an issue against
`STMicroelectronics/stm32n6xx-hal-driver`) -- this isn't specific to this
project or the OV5647, it'll bite anyone bringing up a CSI camera on N6 at
a bit rate under ~1.5 Gbit/s/lane, which is most sensors.

**Memory / `STM32N657X0HXQ_LRUN.ld` (you asked specifically):** checked
this against the STM32N6 boot ROM constraint first (community.st.com
confirms the FSBL-loaded image is hard-capped at 511 KB -- that's a
BootROM/header limit, not a CubeMX setting), and the `ROM` region here is
exactly that: `511K`. `RAM` is `1536K`, and current usage (camera_rgb_frame
600 KB + JPEG buffers ~150 KB + everything else) leaves comfortable
headroom -- not the cause of the lock failure, and not close to
overflowing. If you ever raise the capture resolution, the buffers scale
fast (1280x960 RGB565 alone would be ~2.4 MB, more than all of `RAM`
here), and at that point the fix isn't a bigger number in this file -- the
board has Hexa-SPI PSRAM on it (per UM3417), and this project already
ships `_RAMxspi1`/`_RAMxspi2` linker variants for exactly that move. Not
needed at 640x480.

## Still the same one open question, now testable

`DCMIPP_CSI_PHY_BT_400` (Pipe1 PHY bitrate override some rounds back, now
superseded by the derivation above) and the lane mapping are the only
things left unverified against your actual module. With the HAL bug fixed,
BT_300 should lock if the wiring is right; if `v` stays 0 with `Er`
climbing, bracket BT_275/BT_325 next, then flip `CAM_CSI_LANE_MAPPING` to
`DCMIPP_CSI_INVERTED_DATA_LANES` before re-bracketing BT. Everything else
in this doc from the last three rounds (garbled image, wrong colors,
dark/bright, freezes after one frame, laggy) is unchanged.

Once it's running: `http://<board-ip>/` in a browser, or `ffplay
http://<board-ip>/`.

---

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
