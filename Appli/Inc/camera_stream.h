/**
  ******************************************************************************
  * @file    camera_stream.h
  * @brief   Ties the OV5647 sensor, DCMIPP capture, and hardware JPEG
  *          encoder together into "call Process() in your main loop, pull
  *          out the latest JPEG whenever you want it".
  *
  * Expects CubeMX-generated MX_I2C2_Init(), MX_DCMIPP_Init(), MX_JPEG_Init()
  * to have already run (peripheral config only -- they don't start capture).
  * See CAMERA_INTEGRATION.md for the exact CubeMX settings this assumes.
  ******************************************************************************
  */
#ifndef CAMERA_STREAM_H
#define CAMERA_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>

/* Must match the OV5647 mode (ov5647.c) and the DCMIPP Pipe1 output size
 * configured in CubeMX. */
#define CAMERA_STREAM_WIDTH    640U
#define CAMERA_STREAM_HEIGHT   480U

typedef enum
{
  CAMERA_STREAM_OK = 0,
  CAMERA_STREAM_ERROR_SENSOR_NOT_FOUND,
  CAMERA_STREAM_ERROR_SENSOR_INIT,
  CAMERA_STREAM_ERROR_DCMIPP
} CAMERA_STREAM_StatusTypeDef;

/* Probes and configures the OV5647 over hi2c2, then starts continuous
 * DCMIPP capture on Pipe1. Call once, after MX_I2C2_Init/MX_DCMIPP_Init/
 * MX_JPEG_Init. */
CAMERA_STREAM_StatusTypeDef CAMERA_STREAM_Init(I2C_HandleTypeDef *hi2c2,
                                                DCMIPP_HandleTypeDef *hdcmipp,
                                                JPEG_HandleTypeDef *hjpeg);

/* Call every iteration of the main loop (same spirit as ethernetif_input()
 * / sys_check_timeouts() -- cheap no-op when there's nothing to do, but
 * needs to be called often so the JPEG DMA pump doesn't stall). */
void CAMERA_STREAM_Process(void);

/* Non-blocking accessor for mjpeg_server.c. Pass the frame_id you got last
 * time (0 the first call); returns 1 and fills data/len/frame_id only if a
 * newer frame is ready, so callers can trivially "only send new frames".
 *
 * A successful (return 1) call locks the underlying buffer slot so
 * CAMERA_STREAM_Process() won't let a future capture overwrite it out from
 * under you -- you MUST call CAMERA_STREAM_ReleaseJPEG() with the same
 * pointer once you're done reading it (e.g. once the whole frame has been
 * handed to tcp_write()), or that slot stays locked forever and capture
 * silently stalls after one buffer's worth of frames. */
uint32_t CAMERA_STREAM_GetLatestJPEG(uint32_t last_frame_id, uint8_t **data,
                                      uint32_t *len, uint32_t *frame_id);
void CAMERA_STREAM_ReleaseJPEG(uint8_t *data);

/* Debug/telemetry for camera bring-up (any arg may be NULL):
 *   captured : DCMIPP frames fully received on PIPE1
 *   encoded  : JPEGs encoded
 *   vsync    : PIPE1 frame-start (VSYNC) events -- sensor is driving lanes
 *   errors   : CSI sync + D-PHY line errors -- PHY sees signal it can't decode
 * Interpretation:
 *   vsync=0 captured=0 errors=0 -> no MIPI signal (wiring/power/clock lane)
 *   vsync=0 captured=0 errors>0 -> bit-rate/lane mismatch (bracket PHY_BT_*)
 *   vsync>0 captured=0          -> DCMIPP pixel config, not the PHY
 *   captured>0 encoded=0        -> JPEG encode path, not capture */
void CAMERA_STREAM_GetDebugCounts(uint32_t *captured, uint32_t *encoded,
                                  uint32_t *vsync, uint32_t *errors);

/* Round 4 found and fixed a real ST HAL bug that was breaking D-PHY
 * calibration for every practical PHYBitrate band (see
 * CAMERA_INTEGRATION.md and the FIX comment in stm32n6xx_hal_dcmipp.c
 * near DCMIPP_CSI_WritePHYReg). BT_300 below is the value derived from
 * the OV5647's actual PLL registers (ov5647.c) now that that bug is
 * fixed -- still just the round-4 best guess, though: round 5 (below)
 * doesn't trust it blindly, it's step 0 of the auto-scan. */
#define CAM_CSI_PHY_BITRATE    DCMIPP_CSI_PHY_BT_300   /* derived value; the scan below brackets around/past it automatically if it's wrong */
#define CAM_CSI_LANE_MAPPING   DCMIPP_CSI_PHYSICAL_DATA_LANES /* or _INVERTED_DATA_LANES */

/* ---- CSI auto-negotiation ----------------------------------------------
 * CAMERA_STREAM_Init() no longer just applies CAM_CSI_PHY_BITRATE /
 * CAM_CSI_LANE_MAPPING (main.c) and hopes: if PIPE1 hasn't seen a single
 * vsync ~120ms after that, it starts automatically walking every
 * DCMIPP_CSI_ConfTypeDef combination the CSI-2 receiver supports --
 * PHYBitrate BT_80..BT_2500, both DataLaneMapping values, both
 * NumberOfLanes values (253 combinations total) -- re-applying
 * HAL_DCMIPP_CSI_SetConfig() and watching for the vsync counter to move.
 * This is entirely non-blocking: one step is tried at most every ~120ms,
 * driven from CAMERA_STREAM_Process(), so the main loop/network/OLED all
 * keep running normally during a scan instead of a boot-time freeze.
 *
 * The moment a combination produces a vsync, the scan stops and leaves
 * that config applied (SetConfig already wrote it to hardware -- there's
 * nothing further to "commit"). If a full pass exhausts all 253 without
 * one, that's no longer a software bracketing question -- see
 * CAMERA_INTEGRATION.md. */
typedef struct
{
  uint32_t active;    /* 1 while a scan is in progress                    */
  uint32_t step;      /* combinations tried so far                        */
  uint32_t total;     /* combinations in a full sweep (253)                */
  uint32_t locked;    /* 1 once a combination has produced a vsync         */
  uint32_t mbps;      /* winning PHYBitrate band, as Mbit/s (valid once locked or mid-scan: currently-tried band) */
  uint32_t lanes;      /* 1 or 2 (winning / currently-tried NumberOfLanes) */
  uint32_t inverted;  /* 0 = PHYSICAL, 1 = INVERTED (winning / current)   */
} CAMERA_STREAM_CSIScanStatusTypeDef;

CAMERA_STREAM_CSIScanStatusTypeDef CAMERA_STREAM_GetCSIScanStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_STREAM_H */
