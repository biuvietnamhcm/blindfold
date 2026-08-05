/**
  ******************************************************************************
  * @file    frame_source.c
  * @brief   See frame_source.h.
  *
  * The double-buffer + refcount design (one slot "latest ready" while the
  * other can be safely overwritten once no reader still holds it) is
  * unchanged from camera_stream.c -- it's what let mjpeg_server.c keep
  * streaming one completed frame to a slow client while the next frame
  * landed. Only the producer side changed: FRAME_SOURCE_PushFrame() now
  * takes already-encoded JPEG bytes from an external caller instead of a
  * local hardware JPEG encoder completing a chunked DMA encode, so there's
  * no pump/callback machinery left to drive from FRAME_SOURCE_Process().
  ******************************************************************************
  */
#include "frame_source.h"
#include <string.h>

__attribute__((aligned(32)))
static uint8_t s_jpeg_buf[2][FRAME_SOURCE_MAX_JPEG_SIZE];
static volatile uint32_t s_jpeg_len[2];
static volatile uint32_t s_slot_refcount[2];

static volatile uint8_t  s_latest_ready_slot;   /* 0xFF = nothing ready yet */
static volatile uint32_t s_frame_id;
static volatile uint32_t s_frame_count;

void FRAME_SOURCE_Init(void)
{
  s_jpeg_len[0] = 0;
  s_jpeg_len[1] = 0;
  s_slot_refcount[0] = 0;
  s_slot_refcount[1] = 0;
  s_latest_ready_slot = 0xFFU;
  s_frame_id = 0;
  s_frame_count = 0;
}

void FRAME_SOURCE_Process(void)
{
  /* Nothing to pump -- see the TODO in frame_source.h if you want this to
   * drive a polling-mode SPI receive state machine. */
}

uint32_t FRAME_SOURCE_PushFrame(const uint8_t *data, uint32_t len)
{
  uint32_t target;

  if ((data == NULL) || (len == 0U) || (len > FRAME_SOURCE_MAX_JPEG_SIZE))
  {
    return 0U;  /* mirrors spi_cam_sender.py's own MAX_PAYLOAD drop check */
  }

  /* Same slot-selection rule the old JPEG-encode-complete path used:
   * alternate off whichever slot isn't currently "latest", and refuse to
   * stomp it if a reader (mjpeg_server.c) still has it locked -- drop this
   * frame and let the next call try again, rather than corrupt data out
   * from under a slow client. */
  target = (s_latest_ready_slot == 0xFFU) ? 0U : (uint32_t)(s_latest_ready_slot ^ 1U);
  if (s_slot_refcount[target] > 0U)
  {
    return 0U;
  }

  memcpy(s_jpeg_buf[target], data, len);
  s_jpeg_len[target] = len;
  s_latest_ready_slot = (uint8_t)target;
  s_frame_id++;
  s_frame_count++;

  return 1U;
}

uint32_t FRAME_SOURCE_GetLatestJPEG(uint32_t last_frame_id, uint8_t **data,
                                     uint32_t *len, uint32_t *frame_id)
{
  uint32_t current = s_frame_id;

  *frame_id = current;
  if ((s_latest_ready_slot == 0xFFU) || (current == last_frame_id))
  {
    return 0U;
  }

  s_slot_refcount[s_latest_ready_slot]++;
  *data = s_jpeg_buf[s_latest_ready_slot];
  *len  = s_jpeg_len[s_latest_ready_slot];
  return 1U;
}

void FRAME_SOURCE_ReleaseJPEG(uint8_t *data)
{
  uint32_t slot;

  if (data == s_jpeg_buf[0]) { slot = 0; }
  else if (data == s_jpeg_buf[1]) { slot = 1; }
  else { return; }

  if (s_slot_refcount[slot] > 0U)
  {
    s_slot_refcount[slot]--;
  }
}

uint32_t FRAME_SOURCE_GetFrameCount(void)
{
  return s_frame_count;
}
