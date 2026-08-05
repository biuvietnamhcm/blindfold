/**
  ******************************************************************************
  * @file    frame_source.h
  * @brief   Holds the latest encoded JPEG frame for mjpeg_server.c to serve
  *          over Ethernet, independent of where that frame came from.
  *
  * This replaces camera_stream.c, which used to fill this same double-buffer
  * from a local OV5647 + DCMIPP + hardware JPEG encoder pipeline. That local
  * capture path is gone -- frames are now expected to arrive from an
  * external source instead (e.g. a Raspberry Pi doing its own camera
  * capture + JPEG encode and pushing finished frames over SPI, see
  * spi_cam_sender.py). Nothing in this file talks to SPI, I2C, DCMIPP, or
  * any sensor -- it only stores whatever JPEG bytes FRAME_SOURCE_PushFrame()
  * is given and hands them to mjpeg_server.c on request.
  *
  * TODO (not done here on purpose): wire up your SPI receive code
  * separately and call FRAME_SOURCE_PushFrame() once you have a complete,
  * validated JPEG payload in hand (header parsed, CRC checked -- see the
  * protocol contract in spi_cam_sender.py's docstring). If you end up
  * calling PushFrame() from an interrupt/DMA-complete context rather than
  * from the main loop, note it shares state with FRAME_SOURCE_GetLatestJPEG()
  * / FRAME_SOURCE_ReleaseJPEG(), which run from MJPEG_SERVER_Poll() in the
  * main loop -- either keep PushFrame() on the main-loop side too (have your
  * ISR just set a "payload ready" flag and call PushFrame() from
  * FRAME_SOURCE_Process() instead), or add your own locking around it.
  ******************************************************************************
  */
#ifndef FRAME_SOURCE_H
#define FRAME_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Upper bound on one JPEG frame this module will hold. Matches
 * spi_cam_sender.py's MAX_PAYLOAD -- keep the two in sync if you change
 * either side. */
#define FRAME_SOURCE_MAX_JPEG_SIZE   (64U * 1024U)

/* Resets internal state. Cheap and cannot fail -- there's no hardware to
 * probe anymore. Call once at startup, before MJPEG_SERVER_Init(). */
void FRAME_SOURCE_Init(void);

/* Call every iteration of the main loop (same spirit as ethernetif_input()
 * / sys_check_timeouts()). A no-op today -- kept as a call site for
 * symmetry with the rest of the polling-driven main loop, and as an
 * obvious place to drive a polling-mode SPI receive state machine from
 * later if you don't end up using an interrupt for that instead. */
void FRAME_SOURCE_Process(void);

/* Call this once your SPI receive code has a complete, validated JPEG
 * payload in hand. Copies up to FRAME_SOURCE_MAX_JPEG_SIZE bytes into
 * whichever buffer slot isn't currently locked by a reader and makes it
 * the new "latest" frame.
 * Returns 1 if accepted, 0 if dropped (NULL/zero-length, oversized, or
 * both slots currently locked by a slow reader) -- mirrors the
 * dropped_oversize / dropped_notready bookkeeping already in
 * spi_cam_sender.py, just on this end of the link. */
uint32_t FRAME_SOURCE_PushFrame(const uint8_t *data, uint32_t len);

/* Non-blocking accessor for mjpeg_server.c -- same contract camera_stream.c
 * used to implement. Pass the frame_id you got last time (0 the first
 * call); returns 1 and fills data/len/frame_id only if a newer frame is
 * ready, so callers can trivially "only send new frames".
 *
 * A successful (return 1) call locks the underlying buffer slot so
 * FRAME_SOURCE_PushFrame() won't let a future frame overwrite it out from
 * under you -- you MUST call FRAME_SOURCE_ReleaseJPEG() with the same
 * pointer once you're done reading it, or that slot stays locked forever
 * and new frames silently stop landing. */
uint32_t FRAME_SOURCE_GetLatestJPEG(uint32_t last_frame_id, uint8_t **data,
                                     uint32_t *len, uint32_t *frame_id);
void FRAME_SOURCE_ReleaseJPEG(uint8_t *data);

/* Total frames accepted via FRAME_SOURCE_PushFrame() since Init(). Handy on
 * the OLED as a quick "is anything arriving yet" indicator once SPI is
 * wired up -- main.c already shows it. */
uint32_t FRAME_SOURCE_GetFrameCount(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_SOURCE_H */
