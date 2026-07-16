/**
  ******************************************************************************
  * @file    ov5647.h
  * @brief   OV5647 camera sensor driver for BlindFold.
  *
  * Minimal driver for the OmniVision OV5647 (Raspberry Pi Camera v1 sensor),
  * written against the STM32 BSP CAMERA_Drv_t-style calling convention used
  * by ST's own sensor components (see stm32-bsp-common/camera.h and the
  * imx335 component for the pattern this is modeled on).
  *
  * The OV5647 is a raw Bayer sensor: it has no built-in ISP, so it always
  * outputs RAW8/RAW10 Bayer data over 1 or 2 MIPI CSI-2 lanes. Demosaicing
  * to RGB is done by the STM32N6's DCMIPP pipe, not by the sensor.
  *
  * NOTE ON TUNING: the register table in ov5647.c (ov5647_640x480_raw8[])
  * is a commonly-used community starting point for 640x480 RAW8, 2-lane
  * MIPI output. It has NOT been verified against your specific module —
  * treat first bring-up as a debugging session, not a known-good drop-in.
  * See CAMERA_INTEGRATION.md for what to check if the image doesn't show
  * up or looks wrong.
  ******************************************************************************
  */
#ifndef OV5647_H
#define OV5647_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* I2C 7-bit address 0x36, shifted for HAL's 8-bit DevAddress convention */
#define OV5647_I2C_ADDRESS          (0x36U << 1)

#define OV5647_CHIP_ID              0x5647U
#define OV5647_CHIP_ID_REG_H        0x300AU
#define OV5647_CHIP_ID_REG_L        0x300BU

#define OV5647_WIDTH                640U
#define OV5647_HEIGHT               480U

#define OV5647_OK                   0
#define OV5647_ERROR                (-1)

/* ---- IO function pointers, filled in by the application ---------------- */
typedef int32_t (*OV5647_Init_Func)    (void);
typedef int32_t (*OV5647_DeInit_Func)  (void);
typedef int32_t (*OV5647_GetTick_Func) (void);
typedef int32_t (*OV5647_Delay_Func)   (uint32_t);
typedef int32_t (*OV5647_WriteReg_Func)(uint16_t Addr, uint16_t Reg, uint8_t *pData, uint16_t Length);
typedef int32_t (*OV5647_ReadReg_Func) (uint16_t Addr, uint16_t Reg, uint8_t *pData, uint16_t Length);

typedef struct
{
  OV5647_Init_Func      Init;
  OV5647_DeInit_Func    DeInit;
  uint16_t              Address;
  OV5647_WriteReg_Func  WriteReg;
  OV5647_ReadReg_Func   ReadReg;
  OV5647_GetTick_Func   GetTick;
  OV5647_Delay_Func     Delay;
} OV5647_IO_t;

typedef struct
{
  OV5647_IO_t   IO;
  uint8_t       IsInitialized;
} OV5647_Object_t;

/* ---- Public API ---------------------------------------------------------*/
int32_t OV5647_RegisterBusIO(OV5647_Object_t *pObj, OV5647_IO_t *pIO);
int32_t OV5647_ReadID(OV5647_Object_t *pObj, uint32_t *Id);
int32_t OV5647_Init(OV5647_Object_t *pObj);
int32_t OV5647_DeInit(OV5647_Object_t *pObj);
int32_t OV5647_Start(OV5647_Object_t *pObj);
int32_t OV5647_Stop(OV5647_Object_t *pObj);
int32_t OV5647_MirrorFlipConfig(OV5647_Object_t *pObj, uint32_t Mirror, uint32_t Flip);

/* Manual exposure/gain -- there is no 3A/AWB control loop in this driver,
 * so the image will be a fixed brightness until you call these (or extend
 * the driver with your own auto-exposure loop based on average frame
 * luma -- camera_stream.c leaves a hook for that, see CAMERA_STREAM_Process). */
int32_t OV5647_SetExposure(OV5647_Object_t *pObj, uint16_t ExposureLines);
int32_t OV5647_SetGain(OV5647_Object_t *pObj, uint16_t Gain);

int32_t OV5647_WriteReg(OV5647_Object_t *pObj, uint16_t Reg, uint8_t Value);
int32_t OV5647_ReadReg(OV5647_Object_t *pObj, uint16_t Reg, uint8_t *Value);

#ifdef __cplusplus
}
#endif

#endif /* OV5647_H */
