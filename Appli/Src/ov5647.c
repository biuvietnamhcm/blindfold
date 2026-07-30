/**
  ******************************************************************************
  * @file    ov5647.c
  * @brief   OV5647 camera sensor driver for BlindFold.
  *
  * See ov5647.h for the tuning caveat on the register table below. This
  * configures the sensor for:
  *   - 640x480 output window
  *   - RAW8 Bayer (BGGR), no on-sensor ISP
  *   - 2 MIPI CSI-2 data lanes
  *   - Manual (fixed) exposure/gain -- no AEC/AGC loop running on-sensor
  *     or in this driver, see OV5647_SetExposure/OV5647_SetGain.
  ******************************************************************************
  */
#include "ov5647.h"
#include <stddef.h>

typedef struct
{
  uint16_t reg;
  uint8_t  val;
} ov5647_reg_t;

/* 640x480 RAW8 / 2-lane MIPI. Cross-checked register-by-register against
 * the upstream raspberrypi/linux ov5647.c driver's own 640x480 mode
 * table (the one running real Camera Module V1 units in the field) --
 * found and fixed several value mismatches this table had against that
 * reference: 0x4837 PCLK_PERIOD (0x19->0x24, must match the PLL config,
 * which is otherwise identical between the two tables -- the prime
 * suspect for a corrupted MIPI transmit timing at the source), plus
 * 0x3807/0x3811/0x3813 (windowing/ISP offset) and 0x4004 (BLC), which
 * affect image geometry/black-level rather than PHY lock. Exposure/gain
 * registers intentionally do NOT match that reference -- see the manual
 * exposure/gain note below, that's a deliberate difference, not a bug. */
static const ov5647_reg_t ov5647_640x480_raw8[] =
{
  /* ---- system / PLL --------------------------------------------------- */
  { 0x0100, 0x00 },   /* standby while we configure                        */
  { 0x0103, 0x01 },   /* software reset                                    */
  { 0x3034, 0x08 },   /* MIPI 8-bit raw mode                               */
  { 0x3035, 0x21 },   /* PLL system clock divider                         */
  { 0x3036, 0x46 },   /* PLL multiplier                                    */
  { 0x303c, 0x11 },   /* PLL control                                       */
  { 0x3106, 0xf5 },   /* PLL control / SCLK root divider                   */
  { 0x3821, 0x07 },   /* timing: horizontal mirror + binning on            */
  { 0x3820, 0x41 },   /* timing: vertical flip + binning on                */
  { 0x3827, 0xec },
  { 0x370c, 0x0f },
  { 0x3612, 0x59 },
  { 0x3618, 0x00 },
  { 0x5000, 0x06 },   /* ISP control: LENC/BPC/WPC off for raw passthrough */
  { 0x5002, 0x41 },
  { 0x5003, 0x08 },
  { 0x5a00, 0x08 },
  { 0x3000, 0x00 },
  { 0x3001, 0x00 },
  { 0x3002, 0x00 },
  { 0x3016, 0x08 },
  { 0x3017, 0xe0 },
  { 0x3018, 0x44 },
  { 0x301c, 0xf8 },
  { 0x301d, 0xf0 },
  { 0x3a18, 0x00 },
  { 0x3a19, 0xf8 },
  { 0x3c01, 0x80 },
  { 0x3b07, 0x0c },
  /* ---- timing / windowing (2592x1944 native -> 640x480 via 2x2 binning
   *      and a further scale, matching the ov5647_640x480 mode used
   *      across most open OV5647 integrations) ------------------------- */
  { 0x3800, 0x00 }, { 0x3801, 0x00 },   /* X addr start = 0                */
  { 0x3802, 0x00 }, { 0x3803, 0x00 },   /* Y addr start = 0                */
  { 0x3804, 0x0a }, { 0x3805, 0x3f },   /* X addr end   = 2623             */
  { 0x3806, 0x07 }, { 0x3807, 0xa1 },   /* Y addr end   = 1953 (was 0xa3/1955 --
                                            off by 2 rows vs. the verified
                                            reference table; low-priority
                                            vs. 0x4837, affects crop
                                            geometry not PHY lock)        */
  { 0x3808, 0x02 }, { 0x3809, 0x80 },   /* output width  = 640             */
  { 0x380a, 0x01 }, { 0x380b, 0xe0 },   /* output height = 480             */
  { 0x380c, 0x07 }, { 0x380d, 0x68 },   /* HTS (line length)               */
  { 0x380e, 0x03 }, { 0x380f, 0xd8 },   /* VTS (frame length)              */
  { 0x3810, 0x00 }, { 0x3811, 0x08 },   /* ISP X offset (was 0x10, verified
                                            reference uses 0x08)           */
  { 0x3812, 0x00 }, { 0x3813, 0x02 },   /* ISP Y offset (was 0x06, verified
                                            reference uses 0x02)           */
  { 0x3814, 0x31 },                     /* X odd/even inc (subsampling)    */
  { 0x3815, 0x31 },                     /* Y odd/even inc (subsampling)    */
  { 0x3708, 0x64 },
  { 0x3709, 0x52 },
  { 0x3630, 0x2e },
  { 0x3632, 0xe2 },
  { 0x3633, 0x23 },
  { 0x3634, 0x44 },
  { 0x3636, 0x06 },
  { 0x3620, 0x64 },
  { 0x3621, 0xe0 },
  { 0x3600, 0x37 },
  { 0x3704, 0xa0 },
  { 0x3703, 0x5a },
  { 0x3715, 0x78 },
  { 0x3717, 0x01 },
  { 0x3731, 0x02 },
  { 0x370b, 0x60 },
  { 0x3705, 0x1a },
  { 0x3f05, 0x02 },
  { 0x3f06, 0x10 },
  { 0x3f01, 0x0a },
  /* ---- AEC/AGC: manual mode, fixed exposure/gain (no 3A loop) --------- */
  { 0x3503, 0x03 },   /* bit0 manual gain, bit1 manual exposure            */
  { 0x3500, 0x00 }, { 0x3501, 0x3d }, { 0x3502, 0x00 },  /* exposure       */
  { 0x350a, 0x00 }, { 0x350b, 0x40 },                    /* gain           */
  { 0x4001, 0x02 },
  { 0x4004, 0x02 },   /* BLC line number (was 0x04, verified reference uses 0x02) */
  { 0x4000, 0x09 },
  /* ---- MIPI ------------------------------------------------------------*/
  { 0x4837, 0x24 },    /* PCLK_PERIOD (datasheet: "period of pclk2x,
                          pclk_div=1"). Was 0x19 -- this is a derived
                          value that must match the PLL config above
                          (0x3034/0x3035/0x3036/0x303c/0x3106), not an
                          independent tuning choice, and this project's
                          PLL config is byte-for-byte identical to the
                          upstream raspberrypi/linux ov5647.c driver's,
                          which pairs it with 0x24, not 0x19. A wrong
                          PCLK_PERIOD corrupts the sensor's own MIPI
                          transmit timing at the source -- consistent
                          with the D-PHY receiving near-nothing (mx~2)
                          regardless of what bitrate/lane/polarity the
                          STM32 side tries, since the problem would be
                          upstream of all of that. */
  { 0x4800, 0x04 },    /* MIPI CTRL00: continuous clock (bit5 gating=0).
                          Was 0x24 (gated/non-continuous) -- the STM32N6 CSI
                          D-PHY keeps the clock lane in stop-state with a
                          gated clock and never enters HS RX, which shows up
                          as v:0 c:0 Er:0 (no signal at all). If frames still
                          don't land after this, the next suspect is the
                          22-pin adapter's lane/clock wiring, not a register. */
  /* REMOVED: { 0x300e, 0x45 } was here, commented "MIPI 2-lane enable".
   * Cross-checked against OV5647's own datasheet (table 6-1): 0x300E is
   * SC_CMMN_PAD_SEL0, bits[7:4] explicitly marked "Debug control --
   * Changing these registers is not recommended", bits[3:0] are
   * io_y_sel[11:8]. It has nothing to do with MIPI lane count. The real
   * lane-mode/MIPI-enable control is 0x3018 (SC_CMMN_MIPI_SC_CTRL,
   * bits[7:5] mipi_lane_mode / bit[2] mipi_en), already set correctly
   * above via { 0x3018, 0x44 } -- confirmed against the upstream
   * raspberrypi/linux ov5647.c driver's own 640x480 2-lane init table,
   * which writes this same value. 0x300E is likely a carry-over from a
   * different OmniVision part (0x300E is the real MIPI control register
   * on the OV5640, a similarly-named but different sensor) and isn't
   * touched anywhere in that reference driver, so it's removed here
   * rather than left poking an undocumented register with unknown
   * side effects. */
  { 0x4801, 0x0f },
  { 0x300f, 0x88 },
};

#define OV5647_REG_COUNT  (sizeof(ov5647_640x480_raw8) / sizeof(ov5647_640x480_raw8[0]))

int32_t OV5647_RegisterBusIO(OV5647_Object_t *pObj, OV5647_IO_t *pIO)
{
  if ((pObj == NULL) || (pIO == NULL))
  {
    return OV5647_ERROR;
  }

  pObj->IO = *pIO;
  pObj->IsInitialized = 0;

  if (pObj->IO.Init != NULL)
  {
    if (pObj->IO.Init() != 0)
    {
      return OV5647_ERROR;
    }
  }

  return OV5647_OK;
}

int32_t OV5647_WriteReg(OV5647_Object_t *pObj, uint16_t Reg, uint8_t Value)
{
  if (pObj->IO.WriteReg(pObj->IO.Address, Reg, &Value, 1) != 0)
  {
    return OV5647_ERROR;
  }
  return OV5647_OK;
}

int32_t OV5647_ReadReg(OV5647_Object_t *pObj, uint16_t Reg, uint8_t *Value)
{
  if (pObj->IO.ReadReg(pObj->IO.Address, Reg, Value, 1) != 0)
  {
    return OV5647_ERROR;
  }
  return OV5647_OK;
}

int32_t OV5647_ReadID(OV5647_Object_t *pObj, uint32_t *Id)
{
  uint8_t hi = 0, lo = 0;

  if ((OV5647_ReadReg(pObj, OV5647_CHIP_ID_REG_H, &hi) != OV5647_OK) ||
      (OV5647_ReadReg(pObj, OV5647_CHIP_ID_REG_L, &lo) != OV5647_OK))
  {
    return OV5647_ERROR;
  }

  *Id = ((uint32_t)hi << 8) | (uint32_t)lo;
  return OV5647_OK;
}

int32_t OV5647_Init(OV5647_Object_t *pObj)
{
  uint32_t i;

  for (i = 0; i < OV5647_REG_COUNT; i++)
  {
    if (OV5647_WriteReg(pObj, ov5647_640x480_raw8[i].reg, ov5647_640x480_raw8[i].val) != OV5647_OK)
    {
      return OV5647_ERROR;
    }
    /* The reset write (0x0103) needs the sensor to finish its internal
     * reset sequence before the next register write lands. */
    if (ov5647_640x480_raw8[i].reg == 0x0103 && pObj->IO.Delay != NULL)
    {
      pObj->IO.Delay(5);
    }
  }

  pObj->IsInitialized = 1;
  return OV5647_OK;
}

int32_t OV5647_DeInit(OV5647_Object_t *pObj)
{
  pObj->IsInitialized = 0;
  return OV5647_Stop(pObj);
}

int32_t OV5647_Start(OV5647_Object_t *pObj)
{
  return OV5647_WriteReg(pObj, 0x0100, 0x01);
}

int32_t OV5647_Stop(OV5647_Object_t *pObj)
{
  return OV5647_WriteReg(pObj, 0x0100, 0x00);
}

int32_t OV5647_MirrorFlipConfig(OV5647_Object_t *pObj, uint32_t Mirror, uint32_t Flip)
{
  uint8_t reg3821, reg3820;

  if ((OV5647_ReadReg(pObj, 0x3821, &reg3821) != OV5647_OK) ||
      (OV5647_ReadReg(pObj, 0x3820, &reg3820) != OV5647_OK))
  {
    return OV5647_ERROR;
  }

  if (Mirror != 0U) { reg3821 |= 0x02U; } else { reg3821 &= ~0x02U; }
  if (Flip   != 0U) { reg3820 |= 0x02U; } else { reg3820 &= ~0x02U; }

  if ((OV5647_WriteReg(pObj, 0x3821, reg3821) != OV5647_OK) ||
      (OV5647_WriteReg(pObj, 0x3820, reg3820) != OV5647_OK))
  {
    return OV5647_ERROR;
  }

  return OV5647_OK;
}

int32_t OV5647_SetExposure(OV5647_Object_t *pObj, uint16_t ExposureLines)
{
  /* 20-bit exposure value spread across 0x3500[3:0]/0x3501[7:0]/0x3502[7:4] */
  uint32_t exp = ((uint32_t)ExposureLines) << 4;

  if ((OV5647_WriteReg(pObj, 0x3500, (uint8_t)((exp >> 16) & 0x0FU)) != OV5647_OK) ||
      (OV5647_WriteReg(pObj, 0x3501, (uint8_t)((exp >> 8) & 0xFFU)) != OV5647_OK) ||
      (OV5647_WriteReg(pObj, 0x3502, (uint8_t)(exp & 0xFFU)) != OV5647_OK))
  {
    return OV5647_ERROR;
  }
  return OV5647_OK;
}

int32_t OV5647_SetGain(OV5647_Object_t *pObj, uint16_t Gain)
{
  if ((OV5647_WriteReg(pObj, 0x350a, (uint8_t)((Gain >> 8) & 0x03U)) != OV5647_OK) ||
      (OV5647_WriteReg(pObj, 0x350b, (uint8_t)(Gain & 0xFFU)) != OV5647_OK))
  {
    return OV5647_ERROR;
  }
  return OV5647_OK;
}
