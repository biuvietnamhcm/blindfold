/**
  ******************************************************************************
  * @file    spi_cam_rx.c
  * @brief   See spi_cam_rx.h. This is the receive side frame_source.h flags
  *          as a deliberate TODO: arm SPI5 over DMA, validate
  *          spi_cam_sender.py's FRM1 header + CRC32, and drive READY.
  ******************************************************************************
  */
#include <string.h>

#include "spi_cam_rx.h"
#include "main.h"
#include "frame_source.h"

extern SPI_HandleTypeDef hspi5;

/* Owned entirely by this file (see spi_cam_rx.h). Needs external linkage
 * only because stm32n6xx_it.c's GPDMA1_Channel1_IRQHandler has to reach it;
 * nothing else in the project touches it, so unlike hspi5/hi2c1/etc. it
 * isn't declared in main.c -- it's defined where it's actually used. */
DMA_HandleTypeDef hdma_spi5_rx;

/* -------------------------------------------------------------------------
 * Wire protocol constants -- must match spi_cam_sender.py exactly.
 * ---------------------------------------------------------------------- */
#define CAM_RX_HEADER_LEN   12U   /* struct.calcsize("<4sII") on the Pi side */

/* HAL_SPI_Receive_DMA()'s Size parameter is asserted against
 * IS_SPI_TRANSFER_SIZE(), which requires (SIZE < 0xFFFFU) -- so the
 * largest single DMA-armed SPI transfer this HAL accepts is 0xFFFE (65534)
 * bytes: one less than the uint16_t range, and two less than
 * spi_cam_sender.py's stated MAX_PAYLOAD (65536 == FRAME_SOURCE_MAX_JPEG_SIZE).
 * A frame landing in that 65535-65536 sliver gets rejected here as an
 * oversized header rather than risk truncating a size HAL would
 * misinterpret. In practice this never bites -- a q80 VGA JPEG runs
 * 20-35KB by spi_cam_sender.py's own estimate -- but it's a real,
 * checkable boundary, so it's guarded rather than left to chance. */
#define CAM_RX_MAX_PAYLOAD   0xFFFEU

/* READY, STM32 -> Pi GPIO27. Arduino D8 on CN14: not tied to any peripheral
 * or RIF grant anywhere in this project (confirmed -- grep for PD12/GPIOD
 * across Appli/ turns up nothing), so it can't collide with anything
 * already on the board. */
#define CAM_RX_READY_PORT   GPIOD
#define CAM_RX_READY_PIN    GPIO_PIN_12

/* SPI5_NSS / Pi CE0. hspi5 is configured Slave + NSS_SOFT + NSSPolarity_LOW
 * (see MX_SPI5_Init()); reading HAL_SPI_Init() directly shows that
 * combination leaves the internal SSI bit clear, so the peripheral shifts
 * on SCK unconditionally and never actually samples this pin. That's what
 * makes it safe to reclaim PA3 here as a plain falling-edge EXTI input
 * instead of its CubeMX-assigned AF5_SPI5 role -- SPI5's own byte-shifting
 * never depended on this pin's electrical state, only on a DMA receive
 * being armed, which is unrelated. See SPI_CAM_RX_Init(). */
#define CAM_RX_CS_PORT      GPIOA
#define CAM_RX_CS_PIN       GPIO_PIN_3

/* Comfortably above the worst legitimate transfer: at 8MHz a maximal
 * (0xFFFE-byte) payload takes ~65ms of raw shift time; this leaves >4x
 * margin over that for the inter-write gap and scheduling jitter. If
 * we're still sitting in WAIT_PAYLOAD past this, the master abandoned the
 * transfer mid-stream -- spi_cam_sender.py does exactly that on an
 * OSError -- and nothing more is coming for this frame. */
#define CAM_RX_STUCK_TIMEOUT_MS   300U

typedef enum
{
  CAM_RX_WAIT_HEADER = 0,  /* normal resting state: DMA armed for 12 bytes */
  CAM_RX_WAIT_PAYLOAD      /* valid header parsed, DMA armed for its payload */
} cam_rx_state_t;

static volatile cam_rx_state_t s_state;
static volatile uint32_t       s_pending_len;         /* valid once in WAIT_PAYLOAD */
static volatile uint32_t       s_pending_crc;          /* expected CRC32, ditto */
static volatile uint32_t       s_payload_wait_start;   /* HAL_GetTick() at arm time */

/* Set by HAL_SPI_RxCpltCallback() (ISR context), consumed by
 * SPI_CAM_RX_Process() (main-loop context). This is the "keep PushFrame()
 * on the main-loop side" option frame_source.h's own TODO comment
 * suggests, instead of adding locking of our own around its double-buffer
 * -- FRAME_SOURCE_PushFrame() ends up called from the same loop-driven
 * context as FRAME_SOURCE_GetLatestJPEG()/ReleaseJPEG() (via
 * MJPEG_SERVER_Poll()), never from underneath this ISR. */
static volatile uint8_t        s_payload_ready;
static volatile uint32_t       s_payload_ready_len;

static volatile uint32_t s_bad_header_count;
static volatile uint32_t s_bad_crc_count;
static volatile uint32_t s_spi_error_count;
static volatile uint32_t s_stuck_recovery_count;

/* DMA target buffers. 32-byte aligned to match frame_source.c's own JPEG
 * buffers -- both for consistency and so the cache-line invalidates below
 * never touch a partial line shared with something else. */
static uint8_t s_header_buf[CAM_RX_HEADER_LEN] __attribute__((aligned(32)));
static uint8_t s_rx_buf[FRAME_SOURCE_MAX_JPEG_SIZE] __attribute__((aligned(32)));

static uint32_t s_crc_table[256];

/* -------------------------------------------------------------------------
 * CRC32 -- CRC-32/ISO-HDLC, the exact variant zlib.crc32() implements
 * (poly 0x04C11DB7, reflected 0xEDB88320; init/xorout 0xFFFFFFFF; reflected
 * in and out), which is what spi_cam_sender.py's build_header() uses. Built
 * at init rather than hand-transcribed as a 256-entry table: one mistyped
 * nibble in a pasted table would silently break every frame's CRC check
 * for good, in a way that's genuinely unpleasant to track down later.
 * ---------------------------------------------------------------------- */
static void crc32_build_table(void)
{
  for (uint32_t i = 0U; i < 256U; i++)
  {
    uint32_t c = i;
    for (uint32_t k = 0U; k < 8U; k++)
    {
      c = (c & 1U) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
    }
    s_crc_table[i] = c;
  }
}

static uint32_t crc32_compute(const uint8_t *buf, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;

  for (uint32_t i = 0U; i < len; i++)
  {
    crc = s_crc_table[(crc ^ buf[i]) & 0xFFU] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFUL;
}

static uint32_t rd_u32_le(const uint8_t *p)
{
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* -------------------------------------------------------------------------
 * Arming helpers. Every path that (re)arms a receive needs the same two
 * or three things done together (state, DMA, and for the payload case a
 * fresh watchdog timestamp) -- centralized here instead of repeated at the
 * five call sites below. HAL_SPI_Receive_DMA()'s return isn't checked: by
 * the time any of these run, hspi5.State is guaranteed READY (fresh from
 * Init, or just reset by HAL right before invoking whichever callback got
 * us here), which is Receive_DMA()'s only real failure precondition. If
 * that guarantee were ever violated by a future change elsewhere, the
 * stuck-transfer watchdog in SPI_CAM_RX_Process() still catches the
 * resulting non-progress and re-arms from there.
 * ---------------------------------------------------------------------- */
static void arm_header_rx(void)
{
  s_state = CAM_RX_WAIT_HEADER;
  (void)HAL_SPI_Receive_DMA(&hspi5, s_header_buf, (uint16_t)CAM_RX_HEADER_LEN);
}

static void arm_payload_rx(uint32_t length)
{
  s_state = CAM_RX_WAIT_PAYLOAD;
  s_payload_wait_start = HAL_GetTick();
  (void)HAL_SPI_Receive_DMA(&hspi5, s_rx_buf, (uint16_t)length);
}

/* -------------------------------------------------------------------------
 * HAL callbacks. All three are __weak in the HAL and unoverridden anywhere
 * else in this project (checked), so defining them here is conflict-free.
 * ---------------------------------------------------------------------- */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance != SPI5)
  {
    return;
  }

  if (s_state == CAM_RX_WAIT_HEADER)
  {
    SCB_InvalidateDCache_by_Addr(s_header_buf, (int32_t)sizeof(s_header_buf));

    uint32_t length   = rd_u32_le(&s_header_buf[4]);
    uint32_t crc      = rd_u32_le(&s_header_buf[8]);
    uint8_t  magic_ok = (memcmp(s_header_buf, "FRM1", 4) == 0) ? 1U : 0U;

    if ((magic_ok != 0U) && (length != 0U) && (length <= CAM_RX_MAX_PAYLOAD))
    {
      s_pending_len = length;
      s_pending_crc = crc;
      arm_payload_rx(length);
      /* Still mid-frame -- READY stays low until the payload's in and
       * pushed (or dropped) over in SPI_CAM_RX_Process(). */
    }
    else
    {
      /* Framing is lost (or this was noise). Resync on the next CS pulse
       * rather than try to guess where a real header starts. */
      s_bad_header_count++;
      arm_header_rx();
      HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_SET);
    }
  }
  else /* CAM_RX_WAIT_PAYLOAD */
  {
    uint32_t len = s_pending_len;

    SCB_InvalidateDCache_by_Addr(s_rx_buf, (int32_t)len);

    if (crc32_compute(s_rx_buf, len) == s_pending_crc)
    {
      s_payload_ready_len = len;
      s_payload_ready = 1U;  /* SPI_CAM_RX_Process() takes it from here */
    }
    else
    {
      s_bad_crc_count++;
      arm_header_rx();
      HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_SET);
    }
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance != SPI5)
  {
    return;
  }

  /* OVR/MODF/FRE/UDR all land here. HAL has already disabled the
   * peripheral and torn down the DMA request by this point (see
   * HAL_SPI_IRQHandler()'s error branch) -- arm_header_rx() re-enables
   * everything fresh via HAL_SPI_Receive_DMA(), same as any other resync. */
  s_spi_error_count++;
  arm_header_rx();
  HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_SET);
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == CAM_RX_CS_PIN)
  {
    /* Belt-and-suspenders match for spi_cam_sender.py's documented
     * contract ("drop it the instant it sees CS go low"). In the healthy
     * case READY is already low by the time this fires, since we only
     * ever raise it once the corresponding DMA is armed -- so this is
     * usually a no-op, cheap enough that it costs nothing to keep exact
     * either way. */
    HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_RESET);
  }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void SPI_CAM_RX_Init(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  s_state                = CAM_RX_WAIT_HEADER;
  s_bad_header_count      = 0U;
  s_bad_crc_count         = 0U;
  s_spi_error_count       = 0U;
  s_stuck_recovery_count  = 0U;
  s_payload_ready         = 0U;

  crc32_build_table();

  /* --- READY, PD12 / Arduino D8: brand new pin, port D isn't touched
   * anywhere else on this board. Grant RIF attributes before the pin is
   * ever configured or driven, same ordering SystemIsolation_Config()
   * uses for every other pin in this project. --- */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  HAL_GPIO_ConfigPinAttributes(CAM_RX_READY_PORT, CAM_RX_READY_PIN,
                                GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_RESET);
  gpio_init.Pin   = CAM_RX_READY_PIN;
  gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull  = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CAM_RX_READY_PORT, &gpio_init);

  /* --- Reclaim PA3 (SPI5_NSS) as a plain EXTI input -- see the comment by
   * CAM_RX_CS_PIN's #define for why this is safe. PA3 already carries a
   * GPIO_PIN_SEC|GPIO_PIN_NPRIV grant from SystemIsolation_Config() for
   * its SPI5_NSS role; that grant is per-pin, not per-mode, so it covers
   * this reuse too and doesn't need repeating. --- */
  gpio_init.Pin  = CAM_RX_CS_PIN;
  gpio_init.Mode = GPIO_MODE_IT_FALLING;
  gpio_init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CAM_RX_CS_PORT, &gpio_init);
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  /* --- GPDMA1 Channel1 for SPI5 RX. Channel0 is already spoken for
   * (memory-to-memory, unrelated to this); Channel1 through 15 are all
   * still on their weak default handler, confirming this one's free. --- */
  hdma_spi5_rx.Instance = GPDMA1_Channel1;
  if (HAL_DMA_ConfigChannelAttributes(&hdma_spi5_rx,
                                       DMA_CHANNEL_SEC | DMA_CHANNEL_PRIV |
                                       DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) != HAL_OK)
  {
    Error_Handler();
  }

  hdma_spi5_rx.Init.Request               = GPDMA1_REQUEST_SPI5_RX;
  hdma_spi5_rx.Init.BlkHWRequest          = DMA_BREQ_SINGLE_BURST;
  hdma_spi5_rx.Init.Direction             = DMA_PERIPH_TO_MEMORY;
  hdma_spi5_rx.Init.SrcInc                = DMA_SINC_FIXED;
  hdma_spi5_rx.Init.DestInc               = DMA_DINC_INCREMENTED;
  hdma_spi5_rx.Init.SrcDataWidth          = DMA_SRC_DATAWIDTH_BYTE;
  hdma_spi5_rx.Init.DestDataWidth         = DMA_DEST_DATAWIDTH_BYTE;
  hdma_spi5_rx.Init.Priority              = DMA_HIGH_PRIORITY;
  hdma_spi5_rx.Init.SrcBurstLength        = 1;
  hdma_spi5_rx.Init.DestBurstLength       = 1;
  hdma_spi5_rx.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  hdma_spi5_rx.Init.TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER;
  hdma_spi5_rx.Init.Mode                  = DMA_NORMAL;
  if (HAL_DMA_Init(&hdma_spi5_rx) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_LINKDMA(&hspi5, hdmarx, hdma_spi5_rx);

  HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

  /* Everything's armed -- now, and only now, say so. */
  arm_header_rx();
  HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_SET);
}

void SPI_CAM_RX_Process(void)
{
  if (s_payload_ready != 0U)
  {
    uint32_t len;

    __disable_irq();
    len = s_payload_ready_len;
    s_payload_ready = 0U;
    __enable_irq();

    /* Off the ISR path on purpose -- see the comment above s_payload_ready. */
    (void)FRAME_SOURCE_PushFrame(s_rx_buf, len);

    __disable_irq();
    arm_header_rx();
    __enable_irq();
    HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_SET);
  }

  /* Stuck-transfer watchdog. WAIT_HEADER is our normal idle-and-ready
   * resting state and can legitimately persist forever (e.g. the Pi
   * script simply isn't running yet) -- only WAIT_PAYLOAD, a state that
   * should always resolve within a frame time, is ever timed. */
  if ((s_state == CAM_RX_WAIT_PAYLOAD) &&
      ((HAL_GetTick() - s_payload_wait_start) > CAM_RX_STUCK_TIMEOUT_MS))
  {
    __disable_irq();
    (void)HAL_SPI_Abort(&hspi5);
    arm_header_rx();
    __enable_irq();
    HAL_GPIO_WritePin(CAM_RX_READY_PORT, CAM_RX_READY_PIN, GPIO_PIN_SET);
    s_stuck_recovery_count++;
  }
}

uint32_t SPI_CAM_RX_GetBadHeaderCount(void)     { return s_bad_header_count; }
uint32_t SPI_CAM_RX_GetBadCrcCount(void)        { return s_bad_crc_count; }
uint32_t SPI_CAM_RX_GetSpiErrorCount(void)      { return s_spi_error_count; }
uint32_t SPI_CAM_RX_GetStuckRecoveryCount(void) { return s_stuck_recovery_count; }
