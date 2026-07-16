/**
  ******************************************************************************
  * @file    camera_stream.c
  * @brief   OV5647 -> DCMIPP -> HW JPEG pipeline. See camera_stream.h.
  *
  * The JPEG encode pump (RGB_GetInfo / JPEG_Encode_DMA / the two Handler
  * functions / the HAL_JPEG_*Callback overrides) mirrors the structure of
  * ST's own Examples/JPEG/JPEG_EncodingFromOSPI_DMA reference for the
  * STM32N6 -- that chunked pause/resume pump is how HAL_JPEG's DMA mode
  * has to be driven, it's not a stylistic choice. What's different here is
  * where the RGB input comes from (a live DCMIPP capture buffer instead of
  * a static test image), that a new encode gets kicked off every time a
  * fresh camera frame lands instead of once, and the outer double-buffer
  * (jpeg_out_buf[2]) that lets mjpeg_server.c read one completed frame
  * while the next is being encoded.
  ******************************************************************************
  */
#include "camera_stream.h"
#include "ov5647.h"
#include "jpeg_utils.h"
#include <string.h>

/* ---- Tunables -------------------------------------------------------- */
#define JPEG_QUALITY            80U
#define JPEG_OUT_BUFFER_SIZE     (64U * 1024U)   /* per slot; VGA JPEG at
                                                     quality 80 is usually
                                                     well under this */
#define JPEG_CHUNK_OUT_SIZE      4096U
#define JPEG_INPUT_LINES         16U              /* 4:2:0 -> 16 lines/MCU row */

/* ---- Frame buffers ----------------------------------------------------
 * DCMIPP and the JPEG peripheral both reach these over DMA/AXI, bypassing
 * the CPU cache, so every hand-off between "hardware just wrote this" and
 * "CPU/JPEG is about to read this" needs an explicit cache invalidate.
 * You already hit this class of bug wiring up the ETH DMA descriptors --
 * same idea here. */
__attribute__((aligned(32)))
static uint8_t camera_rgb_frame[CAMERA_STREAM_WIDTH * CAMERA_STREAM_HEIGHT * 2]; /* RGB565 */

__attribute__((aligned(32)))
static uint8_t jpeg_out_buf[2][JPEG_OUT_BUFFER_SIZE];
static volatile uint32_t jpeg_out_len[2] = {0, 0};

/* Small scratch chunks used to pump one encode. These are reused in place
 * (not ping-ponged) -- HAL_JPEG_Pause()/Resume() around each chunk is what
 * makes that safe: HAL won't touch the buffer again until we've drained
 * it and explicitly resumed, matching ST's own JPEG DMA example. */
__attribute__((aligned(32)))
static uint8_t jpeg_in_chunk[CAMERA_STREAM_WIDTH * 2U * JPEG_INPUT_LINES];
__attribute__((aligned(32)))
static uint8_t jpeg_out_chunk[JPEG_CHUNK_OUT_SIZE];

static void cache_invalidate(const void *addr, uint32_t len)
{
  uint32_t start = (uint32_t)addr & ~0x1FUL;
  uint32_t end   = ((uint32_t)addr + len + 31U) & ~0x1FUL;
  SCB_InvalidateDCache_by_Addr((void *)start, (int32_t)(end - start));
}

/* ---- I2C2 IO shim for the OV5647 driver -------------------------------*/
static I2C_HandleTypeDef *s_hi2c2;

static int32_t CAM_IO_Init(void)   { return 0; }
static int32_t CAM_IO_DeInit(void) { return 0; }
static int32_t CAM_IO_GetTick(void) { return (int32_t)HAL_GetTick(); }
static int32_t CAM_IO_Delay(uint32_t ms) { HAL_Delay(ms); return 0; }

static int32_t CAM_IO_WriteReg(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Len)
{
  if (HAL_I2C_Mem_Write(s_hi2c2, DevAddr, Reg, I2C_MEMADD_SIZE_16BIT,
                         pData, Len, 100) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

static int32_t CAM_IO_ReadReg(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Len)
{
  if (HAL_I2C_Mem_Read(s_hi2c2, DevAddr, Reg, I2C_MEMADD_SIZE_16BIT,
                        pData, Len, 100) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

static OV5647_Object_t s_cam;
static DCMIPP_HandleTypeDef *s_hdcmipp;
static JPEG_HandleTypeDef   *s_hjpeg;

/* ---- Frame hand-off between DCMIPP callback and the main loop --------
 * s_encode_slot     : slot the in-progress (or most recently started)
 *                      encode is writing into.
 * s_latest_ready_slot: slot holding the newest COMPLETE frame, or 0xFF
 *                      if none yet -- this is what GetLatestJPEG reads.
 * s_slot_refcount    : how many callers currently hold a pointer into
 *                      that slot via GetLatestJPEG; Process() refuses to
 *                      start an encode into a slot with refcount > 0. */
static volatile uint32_t s_new_capture_ready = 0;
static volatile uint32_t s_encode_busy = 0;
static uint32_t s_capture_frame_id = 0;
static uint32_t s_encoding_frame_id = 0;

static volatile uint32_t s_jpeg_frame_id = 0;
static uint32_t s_encode_slot = 0;
static uint8_t  s_latest_ready_slot = 0xFFU;
static volatile uint8_t s_slot_refcount[2] = {0, 0};

/* ---- JPEG chunked encode pump (see file header) ----------------------*/
typedef struct { uint8_t State; uint8_t *DataBuffer; uint32_t DataBufferSize; } jpeg_buf_t;
#define JBUF_EMPTY 0
#define JBUF_FULL  1

static JPEG_RGBToYCbCr_Convert_Function s_rgb2ycbcr;
static jpeg_buf_t s_in_buf;
static jpeg_buf_t s_out_buf;
static uint32_t s_mcu_total, s_mcu_index;
static volatile uint32_t s_encoding_end;
static volatile uint32_t s_output_paused, s_input_paused;
static JPEG_ConfTypeDef s_jpeg_conf;
static uint32_t s_rgb_index, s_rgb_size;
static uint32_t *s_jpeg_out_cursor;

void RGB_GetInfo(JPEG_ConfTypeDef *pInfo)
{
  pInfo->ImageWidth      = CAMERA_STREAM_WIDTH;
  pInfo->ImageHeight     = CAMERA_STREAM_HEIGHT;
  pInfo->ChromaSubsampling = JPEG_420_SUBSAMPLING;
  pInfo->ColorSpace      = JPEG_YCBCR_COLORSPACE;
  pInfo->ImageQuality    = JPEG_QUALITY;
}

static void jpeg_encode_start(void)
{
  const uint32_t bytes_per_line = CAMERA_STREAM_WIDTH * 2U;
  uint32_t chunk_bytes = bytes_per_line * JPEG_INPUT_LINES;

  RGB_GetInfo(&s_jpeg_conf);
  JPEG_GetEncodeColorConvertFunc(&s_jpeg_conf, &s_rgb2ycbcr, &s_mcu_total);

  s_mcu_index = 0;
  s_encoding_end = 0;
  s_output_paused = 0;
  s_input_paused  = 0;
  s_rgb_index = 0;
  s_rgb_size  = sizeof(camera_rgb_frame);

  s_out_buf.DataBuffer = jpeg_out_chunk;
  s_out_buf.DataBufferSize = 0;
  s_out_buf.State = JBUF_EMPTY;

  s_jpeg_out_cursor = (uint32_t *)jpeg_out_buf[s_encode_slot];

  s_in_buf.DataBuffer = jpeg_in_chunk;
  s_mcu_index += s_rgb2ycbcr(&camera_rgb_frame[s_rgb_index], s_in_buf.DataBuffer, 0,
                              chunk_bytes, &s_in_buf.DataBufferSize);
  s_in_buf.State = JBUF_FULL;
  s_rgb_index += chunk_bytes;

  HAL_JPEG_ConfigEncoding(s_hjpeg, &s_jpeg_conf);
  HAL_JPEG_Encode_DMA(s_hjpeg, s_in_buf.DataBuffer, s_in_buf.DataBufferSize,
                       s_out_buf.DataBuffer, JPEG_CHUNK_OUT_SIZE);
}

static void jpeg_encode_input_pump(void)
{
  const uint32_t bytes_per_line = CAMERA_STREAM_WIDTH * 2U;
  uint32_t chunk_bytes = bytes_per_line * JPEG_INPUT_LINES;

  if ((s_in_buf.State == JBUF_EMPTY) && (s_mcu_index <= s_mcu_total))
  {
    if (s_rgb_index < s_rgb_size)
    {
      s_in_buf.DataBuffer = jpeg_in_chunk;
      s_mcu_index += s_rgb2ycbcr(&camera_rgb_frame[s_rgb_index], s_in_buf.DataBuffer, 0,
                                  chunk_bytes, &s_in_buf.DataBufferSize);
      s_in_buf.State = JBUF_FULL;
      s_rgb_index += chunk_bytes;

      if (s_input_paused == 1U)
      {
        s_input_paused = 0;
        HAL_JPEG_ConfigInputBuffer(s_hjpeg, s_in_buf.DataBuffer, s_in_buf.DataBufferSize);
        HAL_JPEG_Resume(s_hjpeg, JPEG_PAUSE_RESUME_INPUT);
      }
    }
    else
    {
      s_mcu_index++;
    }
  }
}

/* Returns 1 once the frame is fully encoded. */
static uint32_t jpeg_encode_output_pump(void)
{
  if (s_out_buf.State == JBUF_FULL)
  {
    uint32_t room = JPEG_OUT_BUFFER_SIZE - (uint32_t)((uint8_t *)s_jpeg_out_cursor - jpeg_out_buf[s_encode_slot]);
    uint32_t n = s_out_buf.DataBufferSize;

    if (n > room)
    {
      n = room;   /* frame too big for the slot -- gets truncated instead of
                     overrunning; if you hit this, raise JPEG_OUT_BUFFER_SIZE
                     or drop JPEG_QUALITY. */
    }
    memcpy(s_jpeg_out_cursor, s_out_buf.DataBuffer, n);
    s_jpeg_out_cursor = (uint32_t *)((uint8_t *)s_jpeg_out_cursor + n);

    s_out_buf.State = JBUF_EMPTY;
    s_out_buf.DataBufferSize = 0;

    if (s_encoding_end != 0U)
    {
      return 1;
    }
    if ((s_output_paused == 1U) && (s_out_buf.State == JBUF_EMPTY))
    {
      s_output_paused = 0;
      HAL_JPEG_Resume(s_hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
    }
  }
  return 0;
}

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t NbEncodedData)
{
  if (NbEncodedData == s_in_buf.DataBufferSize)
  {
    s_in_buf.State = JBUF_EMPTY;
    s_in_buf.DataBufferSize = 0;
    HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_INPUT);
    s_input_paused = 1;
  }
  else
  {
    HAL_JPEG_ConfigInputBuffer(hjpeg, s_in_buf.DataBuffer + NbEncodedData,
                                s_in_buf.DataBufferSize - NbEncodedData);
  }
}

void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg, uint8_t *pDataOut, uint32_t OutDataLength)
{
  /* pDataOut is the buffer HAL just finished filling -- with a single
   * reused chunk buffer this is always &jpeg_out_chunk[0], but we take
   * the HAL's word for it rather than assume. */
  s_out_buf.DataBuffer = pDataOut;
  s_out_buf.DataBufferSize = OutDataLength;
  s_out_buf.State = JBUF_FULL;

  HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
  s_output_paused = 1;

  /* Not resumed here -- jpeg_encode_output_pump() resumes once it has
   * copied this chunk out, so HAL never writes over data we haven't
   * drained yet. */
  HAL_JPEG_ConfigOutputBuffer(hjpeg, jpeg_out_chunk, JPEG_CHUNK_OUT_SIZE);
}

void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
  (void)hjpeg;
  s_encoding_end = 1;
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg)
{
  (void)hjpeg;
  s_encoding_end = 1;   /* drop this frame, next capture will retry */
  s_encode_busy = 0;
}

/* ---- DCMIPP frame-ready callback --------------------------------------*/
void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  if (Pipe == DCMIPP_PIPE1)
  {
    cache_invalidate(camera_rgb_frame, sizeof(camera_rgb_frame));
    s_capture_frame_id++;
    s_new_capture_ready = 1;
  }
}

/* ---- Public API --------------------------------------------------------*/
CAMERA_STREAM_StatusTypeDef CAMERA_STREAM_Init(I2C_HandleTypeDef *hi2c2,
                                                DCMIPP_HandleTypeDef *hdcmipp,
                                                JPEG_HandleTypeDef *hjpeg)
{
  OV5647_IO_t io;
  uint32_t id = 0;

  s_hi2c2 = hi2c2;
  s_hdcmipp = hdcmipp;
  s_hjpeg = hjpeg;

  io.Init     = CAM_IO_Init;
  io.DeInit   = CAM_IO_DeInit;
  io.Address  = OV5647_I2C_ADDRESS;
  io.WriteReg = CAM_IO_WriteReg;
  io.ReadReg  = CAM_IO_ReadReg;
  io.GetTick  = CAM_IO_GetTick;
  io.Delay    = CAM_IO_Delay;

  if (OV5647_RegisterBusIO(&s_cam, &io) != OV5647_OK)
  {
    return CAMERA_STREAM_ERROR_SENSOR_NOT_FOUND;
  }

  if ((OV5647_ReadID(&s_cam, &id) != OV5647_OK) || (id != OV5647_CHIP_ID))
  {
    /* Wrong/no ACK from 0x6C on I2C2 -- check the FPC seating and CN6
     * pinout (Table 15 in UM3417: I2C2_SCL=PB10, I2C2_SDA=PB11) before
     * assuming the driver is wrong. */
    return CAMERA_STREAM_ERROR_SENSOR_NOT_FOUND;
  }

  if (OV5647_Init(&s_cam) != OV5647_OK)
  {
    return CAMERA_STREAM_ERROR_SENSOR_INIT;
  }

  if (OV5647_Start(&s_cam) != OV5647_OK)
  {
    return CAMERA_STREAM_ERROR_SENSOR_INIT;
  }

  /* Continuous single-buffer capture, matching ST's DCMIPP_ContinuousMode
   * pattern: DCMIPP keeps overwriting camera_rgb_frame every frame. There's
   * a small tearing risk if a JPEG encode is still reading late lines of
   * the previous frame when the next one starts landing -- fine for a
   * first working version; HAL_DCMIPP_PIPE_DoubleBufferStart is the
   * upgrade path if you see tearing in practice. */
  if (HAL_DCMIPP_CSI_PIPE_Start(s_hdcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0,
                                 (uint32_t)camera_rgb_frame, DCMIPP_MODE_CONTINUOUS) != HAL_OK)
  {
    return CAMERA_STREAM_ERROR_DCMIPP;
  }

  return CAMERA_STREAM_OK;
}

void CAMERA_STREAM_Process(void)
{
  if (s_encode_busy)
  {
    jpeg_encode_input_pump();
    if (jpeg_encode_output_pump() != 0U)
    {
      jpeg_out_len[s_encode_slot] = (uint32_t)((uint8_t *)s_jpeg_out_cursor - jpeg_out_buf[s_encode_slot]);
      s_latest_ready_slot = (uint8_t)s_encode_slot;
      s_jpeg_frame_id = s_encoding_frame_id;
      s_encode_busy = 0;
    }
    return;
  }

  if (s_new_capture_ready)
  {
    uint32_t target = s_encode_slot ^ 1U;

    if (s_slot_refcount[target] > 0U)
    {
      /* A reader is still draining this slot from two frames ago (slow
       * network / stalled client). Drop this capture and try again next
       * frame rather than corrupt data out from under them. */
      return;
    }

    s_new_capture_ready = 0;
    s_encode_slot = target;
    s_encoding_frame_id = s_capture_frame_id;
    s_encode_busy = 1;
    jpeg_encode_start();
  }
}

uint32_t CAMERA_STREAM_GetLatestJPEG(uint32_t last_frame_id, uint8_t **data,
                                      uint32_t *len, uint32_t *frame_id)
{
  uint32_t current = s_jpeg_frame_id;

  *frame_id = current;
  if ((s_latest_ready_slot == 0xFFU) || (current == last_frame_id))
  {
    return 0;
  }

  s_slot_refcount[s_latest_ready_slot]++;
  *data = jpeg_out_buf[s_latest_ready_slot];
  *len  = jpeg_out_len[s_latest_ready_slot];
  return 1;
}

void CAMERA_STREAM_ReleaseJPEG(uint8_t *data)
{
  uint32_t slot;

  if (data == jpeg_out_buf[0]) { slot = 0; }
  else if (data == jpeg_out_buf[1]) { slot = 1; }
  else { return; }

  if (s_slot_refcount[slot] > 0U)
  {
    s_slot_refcount[slot]--;
  }
}
