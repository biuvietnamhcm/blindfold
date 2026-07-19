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

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_STREAM_H */
