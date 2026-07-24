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

/* ---- Camera control lines (CN6, UM3417 Rev 3 Table 15) --------------
 * The OV5647 module's power and reset are gated by two GPIOs that the
 * generated CubeMX code never drives. Until PWR_EN is high and NRST_CAM
 * is released, the sensor is unpowered / held in reset and will not ACK
 * on I2C2 -- which surfaces as CAMERA_STREAM_ERROR_SENSOR_NOT_FOUND
 * ("CAM: FAIL" on the OLED). CAM_PowerUp() below is what actually turns
 * the sensor on; it must run before the first I2C access. */
#define CAM_PWREN_PORT   GPIOA
#define CAM_PWREN_PIN    GPIO_PIN_0     /* CN6 pin 18, PWR_EN   */
#define CAM_NRST_PORT    GPIOO
#define CAM_NRST_PIN     GPIO_PIN_5     /* CN6 pin 17, NRST_CAM */

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

/* ---- DCMIPP bring-up diagnostics --------------------------------------
 * These counters exist purely to tell apart *why* no frame lands, without
 * a debugger. All three interrupt sources are already enabled by the HAL
 * (PIPE1 vsync/frame by HAL_DCMIPP_CSI_PIPE_Start; CSI sync + D-PHY errors
 * by HAL_DCMIPP_CSI_SetConfig), so these weak-override callbacks just
 * count:
 *   s_vsync_count : DCMIPP saw a frame START on PIPE1 (sensor IS driving
 *                   the MIPI lanes and the PHY locked at least to a SOF).
 *   s_capture_frame_id : a full frame completed (the goal).
 *   s_csi_err_count : CSI sync / D-PHY line errors -- the PHY is receiving
 *                   something but can't decode it (bit-rate/timing/lane).
 *
 * Reading them together:
 *   v=0  c=0  Er=0 -> PHY sees nothing: sensor not streaming, clock lane,
 *                     power/reset, or adapter/FPC wiring. Not the bit-rate.
 *   v=0  c=0  Er>0 -> PHY sees signal but can't sync: bit-rate/lane/polarity
 *                     -- the computed rate is 291.67 Mbit/s/lane so the band
 *                     is BT_300 (see main.c); bracket BT_275 / BT_325, or
 *                     the MB1723 adapter has swapped lane order/polarity.
 *   v>0  c=0       -> frames start but never complete: DCMIPP pixel config
 *                     (data type / packing / line count), not the PHY.
 *   v>0  c>0       -> capture works; problem is downstream (JPEG/stream). */
static volatile uint32_t s_vsync_count = 0;
static volatile uint32_t s_csi_err_count = 0;

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

void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  (void)hdcmipp;
  if (Pipe == DCMIPP_PIPE1) { s_vsync_count++; }
}

void HAL_DCMIPP_PIPE_ErrorCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  (void)hdcmipp; (void)Pipe;
  s_csi_err_count++;
}

void HAL_DCMIPP_ErrorCallback(DCMIPP_HandleTypeDef *hdcmipp)
{
  (void)hdcmipp;
  s_csi_err_count++;
}

void HAL_DCMIPP_CSI_LineErrorCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t DataLane)
{
  (void)hdcmipp; (void)DataLane;
  s_csi_err_count++;
}

/* ---- CSI auto-negotiation ----------------------------------------------
 * v=0/Er climbing at CAM_CSI_PHY_BITRATE/CAM_CSI_LANE_MAPPING (main.c)
 * means that guess is wrong, not that the D-PHY is unfixable -- so instead
 * of another manual edit/rebuild/reflash cycle, walk every combination it
 * supports and let the hardware say which one is right.
 *
 * DCMIPP_CSI_PHY_BT_80..BT_2500 are contiguous integers (0..62 -- see
 * IS_DCMIPP_CSI_DATA_PHY_BITRATE in stm32n6xx_hal_dcmipp.h), so "try every
 * band" is just "try every value 0..62" -- no need to name each one.
 * csi_bitrate_mbps[] below exists purely so status/logging can show
 * "300Mbps" instead of a meaningless index; it's indexed by that same
 * raw PHYBitrate value.
 *
 * One tick (csi_scan_tick(), called from CAMERA_STREAM_Process() every
 * main-loop iteration) applies at most one new candidate every
 * CSI_SCAN_SETTLE_MS -- long enough for a few frames at any plausible
 * sensor frame rate, short enough that a full 253-combination sweep is
 * ~30s, not a boot-time freeze. Candidate order: current main.c value
 * first (step 0, free if it's already right), then every PHYBitrate x
 * DataLaneMapping at NumberOfLanes=TWO (what the sensor is actually
 * configured to transmit), then the same sweep again at NumberOfLanes=ONE
 * as a last resort in case the adapter only actually wires one lane
 * through. */
#define CSI_SCAN_SETTLE_MS     120U
#define CSI_SCAN_BITRATE_COUNT (DCMIPP_CSI_PHY_BT_2500 + 1U)              /* 63  */
#define CSI_SCAN_PER_LANE_STAGE (2U * CSI_SCAN_BITRATE_COUNT)             /* 126 (x2 DataLaneMapping) */
#define CSI_SCAN_TOTAL_STEPS   (1U + 2U * CSI_SCAN_PER_LANE_STAGE)        /* 253 (x2 NumberOfLanes)   */

static const uint16_t csi_bitrate_mbps[CSI_SCAN_BITRATE_COUNT] =
{
   80,  90, 100, 110, 120, 130, 140, 150, 160, 170,
  180, 190, 205, 220, 235, 250, 275, 300, 325, 350,
  400, 450, 500, 550, 600, 650, 700, 750, 800, 850,
  900, 950,1000,1050,1100,1150,1200,1250,1300,1350,
 1400,1450,1500,1550,1600,1650,1700,1750,1800,1850,
 1900,1950,2000,2050,2100,2150,2200,2250,2300,2350,
 2400,2450,2500
};

static volatile uint32_t s_csi_scan_active = 0;   /* armed by csi_scan_start(), called from CAMERA_STREAM_Init() */
static volatile uint32_t s_csi_scan_locked = 0;
static uint32_t s_csi_scan_step = 0;
static uint32_t s_csi_scan_deadline = 0;
static uint32_t s_csi_scan_vsync_baseline = 0;
static DCMIPP_CSI_ConfTypeDef s_csi_scan_cfg = {0};   /* current / winning combination */

/* step 0 = whatever main.c is currently built with; step 1.. = the
 * systematic sweep described above. */
static void csi_scan_build_candidate(uint32_t step, DCMIPP_CSI_ConfTypeDef *cfg)
{
  if (step == 0U)
  {
    cfg->PHYBitrate      = CAM_CSI_PHY_BITRATE;
    cfg->DataLaneMapping = CAM_CSI_LANE_MAPPING;
    cfg->NumberOfLanes   = DCMIPP_CSI_TWO_DATA_LANES;
  }
  else
  {
    uint32_t idx         = step - 1U;
    uint32_t lane_stage  = idx / CSI_SCAN_PER_LANE_STAGE;   /* 0 = two lanes, 1 = one lane */
    uint32_t rem         = idx % CSI_SCAN_PER_LANE_STAGE;

    cfg->PHYBitrate      = rem / 2U;
    cfg->DataLaneMapping = ((rem % 2U) == 0U) ? DCMIPP_CSI_PHYSICAL_DATA_LANES
                                               : DCMIPP_CSI_INVERTED_DATA_LANES;
    cfg->NumberOfLanes   = (lane_stage == 0U) ? DCMIPP_CSI_TWO_DATA_LANES
                                               : DCMIPP_CSI_ONE_DATA_LANE;
  }
}

static void csi_scan_apply(uint32_t step)
{
  csi_scan_build_candidate(step, &s_csi_scan_cfg);
  s_csi_scan_vsync_baseline = s_vsync_count;
  (void)HAL_DCMIPP_CSI_SetConfig(s_hdcmipp, &s_csi_scan_cfg);
  s_csi_scan_deadline = HAL_GetTick() + CSI_SCAN_SETTLE_MS;
}

/* Arms the scan. Deliberately does no waiting itself -- csi_scan_tick()
 * (from CAMERA_STREAM_Process(), i.e. the main loop) does all the timed
 * work, so CAMERA_STREAM_Init() stays fast and nothing else in main()
 * stalls behind a bring-up scan. */
static void csi_scan_start(void)
{
#if CAM_CSI_AUTO_SCAN_ENABLE
  s_csi_scan_active = 1;
  s_csi_scan_locked = 0;
  s_csi_scan_step = 0;
  csi_scan_apply(0);
#else
  /* Manual mode: apply CAM_CSI_PHY_BITRATE/CAM_CSI_LANE_MAPPING through
   * the exact same step-0 path the scan would use, then stop -- never
   * arm s_csi_scan_active, so csi_scan_tick() stays a no-op forever and
   * nothing else touches CSI config from here on. main.c's OLED display
   * doesn't consult scan status at all in this mode (see the #if
   * CAM_CSI_AUTO_SCAN_ENABLE guard around that block there), so there's
   * no "never scanned" state to represent -- it just isn't asked. */
  csi_scan_apply(0);
#endif
}

static void csi_scan_tick(void)
{
  if (!s_csi_scan_active)
  {
    return;
  }
  if ((int32_t)(HAL_GetTick() - s_csi_scan_deadline) < 0)
  {
    return;   /* still settling the current candidate */
  }

  if (s_vsync_count != s_csi_scan_vsync_baseline)
  {
    /* This combination produced a real frame-start. It's already applied
     * in hardware (HAL_DCMIPP_CSI_SetConfig above) -- just stop. */
    s_csi_scan_active = 0;
    s_csi_scan_locked = 1;
    return;
  }

  s_csi_scan_step++;
  if (s_csi_scan_step >= CSI_SCAN_TOTAL_STEPS)
  {
    /* Every PHYBitrate x DataLaneMapping x NumberOfLanes combination the
     * receiver supports has been tried without a single vsync. See
     * CAMERA_INTEGRATION.md -- at this point it's a wiring/adapter/sensor
     * question, not a config one. */
    s_csi_scan_active = 0;
    s_csi_scan_locked = 0;
    return;
  }
  csi_scan_apply(s_csi_scan_step);
}

CAMERA_STREAM_CSIScanStatusTypeDef CAMERA_STREAM_GetCSIScanStatus(void)
{
  CAMERA_STREAM_CSIScanStatusTypeDef s;

  s.active   = s_csi_scan_active;
  s.step     = s_csi_scan_step;
  s.total    = CSI_SCAN_TOTAL_STEPS;
  s.locked   = s_csi_scan_locked;
  s.mbps     = csi_bitrate_mbps[s_csi_scan_cfg.PHYBitrate];
  s.lanes    = (s_csi_scan_cfg.NumberOfLanes == DCMIPP_CSI_TWO_DATA_LANES) ? 2U : 1U;
  s.inverted = (s_csi_scan_cfg.DataLaneMapping == DCMIPP_CSI_INVERTED_DATA_LANES) ? 1U : 0U;
  return s;
}

/* Power the OV5647 up and release it from reset. Must run before any I2C
 * access to the sensor. Timing is deliberately generous: the module's
 * onboard regulators/oscillator need to settle, and the OV5647 wants
 * >~20 ms after reset release before it answers on the SCCB/I2C bus. */
static void CAM_PowerUp(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();

  /* RIF: this app runs Secure/CID1, so give these two pins the same
   * secure attribute the rest of the board's GPIOs get in
   * SystemIsolation_Config() -- otherwise the writes below can be
   * fenced by the security fabric. */
  HAL_GPIO_ConfigPinAttributes(CAM_PWREN_PORT, CAM_PWREN_PIN, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(CAM_NRST_PORT,  CAM_NRST_PIN,  GPIO_PIN_SEC | GPIO_PIN_NPRIV);

  /* Pre-load ODR to the known-off state before switching the pins to
   * outputs, so they don't glitch high during HAL_GPIO_Init(). */
  HAL_GPIO_WritePin(CAM_PWREN_PORT, CAM_PWREN_PIN, GPIO_PIN_RESET); /* power off  */
  HAL_GPIO_WritePin(CAM_NRST_PORT,  CAM_NRST_PIN,  GPIO_PIN_RESET); /* in reset   */

  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin   = CAM_PWREN_PIN;
  HAL_GPIO_Init(CAM_PWREN_PORT, &gpio);
  gpio.Pin   = CAM_NRST_PIN;
  HAL_GPIO_Init(CAM_NRST_PORT, &gpio);

  HAL_Delay(5);
  HAL_GPIO_WritePin(CAM_PWREN_PORT, CAM_PWREN_PIN, GPIO_PIN_SET);   /* enable power  */
  HAL_Delay(10);
  HAL_GPIO_WritePin(CAM_NRST_PORT,  CAM_NRST_PIN,  GPIO_PIN_SET);   /* release reset */
  HAL_Delay(20);                                                    /* sensor boot   */
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

  /* Turn the sensor on and release it from reset before any I2C access,
   * otherwise the chip-ID read below NACKs -> "CAM: FAIL". */
  CAM_PowerUp();

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

  /* RAW8 Bayer -> RGB565 needs the Pipe1 ISP demosaicing (RawBayer2RGB)
   * block explicitly configured and enabled. MX_DCMIPP_Init() sets the
   * pixel packer to RGB565 but the demosaic block defaults to bypassed,
   * so without this the RGB packer just receives raw Bayer bytes and the
   * image comes out garbled/green. (This does NOT gate frame capture --
   * v:0/c:0 is a CSI D-PHY lock problem upstream of here -- but the image
   * is unusable until demosaicing is on, so configure it before capture.)
   *
   * Bayer order: the sensor table sets mirror+flip (0x3821/0x3820), which
   * swaps the Bayer phase; BGGR is the starting guess for this mode. If a
   * captured frame shows a strong magenta/green checkerboard cast, this is
   * the knob to rotate (RGGB / GRBG / GBRG). */
  {
    DCMIPP_RawBayer2RGBConfTypeDef demosaic = {0};
    demosaic.RawBayerType  = DCMIPP_RAWBAYER_BGGR;
    demosaic.VLineStrength = DCMIPP_RAWBAYER_ALGO_STRENGTH_8;
    demosaic.HLineStrength = DCMIPP_RAWBAYER_ALGO_STRENGTH_8;
    demosaic.PeakStrength  = DCMIPP_RAWBAYER_ALGO_STRENGTH_8;
    demosaic.EdgeStrength  = DCMIPP_RAWBAYER_ALGO_STRENGTH_8;
    if (HAL_DCMIPP_PIPE_SetISPRawBayer2RGBConfig(s_hdcmipp, DCMIPP_PIPE1, &demosaic) != HAL_OK)
    {
      return CAMERA_STREAM_ERROR_DCMIPP;
    }
    if (HAL_DCMIPP_PIPE_EnableISPRawBayer2RGB(s_hdcmipp, DCMIPP_PIPE1) != HAL_OK)
    {
      return CAMERA_STREAM_ERROR_DCMIPP;
    }
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

  /* Pipe is live and watching for a frame start. Arm the CSI auto-scan
   * (see the block above HAL_DCMIPP_PIPE_ErrorCallback) instead of just
   * trusting CAM_CSI_PHY_BITRATE/CAM_CSI_LANE_MAPPING blind -- if those
   * are wrong, CAMERA_STREAM_Process() will walk every other combination
   * on its own from here. */
  csi_scan_start();

  return CAMERA_STREAM_OK;
}

void CAMERA_STREAM_Process(void)
{
  /* Cheap (one tick compare) except on the ~120ms boundaries where it
   * actually applies the next candidate -- see csi_scan_tick() above. */
  csi_scan_tick();

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

void CAMERA_STREAM_GetDebugCounts(uint32_t *captured, uint32_t *encoded,
                                  uint32_t *vsync, uint32_t *errors)
{
  if (captured != NULL) { *captured = s_capture_frame_id; }
  if (encoded  != NULL) { *encoded  = s_jpeg_frame_id; }
  if (vsync    != NULL) { *vsync    = s_vsync_count; }
  if (errors   != NULL) { *errors   = s_csi_err_count; }
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
