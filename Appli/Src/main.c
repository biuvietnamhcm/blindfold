/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sh1106.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"
#include "ethernetif.h"
#include "app_ethernet.h"
#include "net_display.h"
#include "camera_stream.h"
#include "mjpeg_server.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* CAM_CSI_PHY_BITRATE / CAM_CSI_LANE_MAPPING moved to camera_stream.h:
 * camera_stream.c's auto-scan (round 5) needs them as its step-0
 * candidate, and this file isn't a shared header. Full history (the
 * round-4 HAL bug, why BT_300, the round-5 auto-scan) is there and in
 * CAMERA_INTEGRATION.md. */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
#if defined ( __ICCARM__ ) /*!< IAR Compiler */
#pragma location=0x34100000
ETH_DMADescTypeDef  DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
#pragma location=0x341000C0
ETH_DMADescTypeDef  DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __CC_ARM )  /* MDK ARM Compiler */

__attribute__((at(0x34100000))) ETH_DMADescTypeDef  DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
__attribute__((at(0x341000C0))) ETH_DMADescTypeDef  DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __GNUC__ ) /* GNU Compiler */

ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT] __attribute__((section(".RxDecripSection"))); /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT] __attribute__((section(".TxDecripSection")));   /* Ethernet Tx DMA Descriptors */
#endif

ETH_TxPacketConfigTypeDef TxConfig;

COM_InitTypeDef BspCOMInit;

DCMIPP_HandleTypeDef hdcmipp;

ETH_HandleTypeDef heth1;

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;

JPEG_HandleTypeDef hjpeg;

/* USER CODE BEGIN PV */
struct netif gnetif;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void MX_GPIO_Init(void);
static void MX_ETH1_Init(void);
static void MX_I2C1_Init(void);
static void MX_DCMIPP_Init(void);
static void MX_JPEG_Init(void);
static void MX_I2C2_Init(void);
static void SystemIsolation_Config(void);
/* USER CODE BEGIN PFP */
static void Netif_Config(void);
static void BlinkBlue(uint8_t count);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */
  BSP_LED_Init(LED_RED);
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_BLUE);
  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* RIF grants must land before any peripheral driven by CID1 touches
   * its own registers -- SystemIsolation_Config() is auto-called again
   * later (after MX_DCMIPP_Init()/MX_JPEG_Init()) by CubeMX's generated
   * sequence; calling it here too, first, fixes that ordering. The
   * later call just re-applies the same config, which is harmless. */
  SystemIsolation_Config();

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ETH1_Init();
  MX_I2C1_Init();
  MX_DCMIPP_Init();
  MX_JPEG_Init();
  MX_I2C2_Init();
  SystemIsolation_Config();
  /* USER CODE BEGIN 2 */

  for(int i = 0; i < 10; i++){
    BSP_LED_Toggle(LED_GREEN);
    HAL_Delay(50);
  }
  SH1106_Status oled_status = SH1106_Init(&hi2c1);

  /* Dashboard layout (128x64, 8px rows, no border so every row gets the
   * full 16-character width):
   *   y=0  : title (static, drawn once)
   *   y=16..32 : live network status, owned by net_display.c
   *   y=48 : uptime counter, owned by the main loop below
   */
  if (oled_status == SH1106_OK)
  {
    SH1106_Fill(SH1106_COLOR_BLACK);
    SH1106_SetCursor(0, 0);
    SH1106_WriteString("BlindFold", SH1106_COLOR_WHITE);

    /* Row 8 is the one row on this display nothing else claims (0=title,
     * 16/24/32=net_display.c, 40=net debug, 48/56=main loop below) -- see
     * chat notes on the y=32 collision with the CAM: status line.
     *
     * This line reads the same two macros MX_DCMIPP_Init() actually
     * assigns into pCSI_Config a few lines below, so it always shows
     * what THIS BINARY was built with, not what main.c currently says on
     * someone's disk. Cross-check against the table in this thread:
     * PHY 16/17/18/20 = BT_275/300/325/400, LANE 1/2 = physical/inverted. */
    {
      char build_line[17];
      snprintf(build_line, sizeof(build_line), "PHY:%lu LN:%lu",
                (unsigned long)CAM_CSI_PHY_BITRATE, (unsigned long)CAM_CSI_LANE_MAPPING);
      SH1106_SetCursor(0, 8);
      SH1106_WriteString(build_line, SH1106_COLOR_WHITE);
    }

    NetDisplay_ShowStatus("ETH: init...", NULL, NULL);
  }
  else
  {
    /* No ACK from the OLED (wrong wiring, missing pull-ups, or wrong
     * address) -- fast-blink red so it's obvious at a glance, then
     * keep going instead of hanging forever. */
    for (uint8_t i = 0; i < 10; i++)
    {
      BSP_LED_Toggle(LED_RED);
      HAL_Delay(80);
    }
    BSP_LED_Off(LED_RED);
  }
  BSP_LED_Toggle(LED_GREEN);

  /* Bring up the TCP/IP stack in NO_SYS (polling) mode -- no RTOS task
   * services it, the main loop below drives it directly. */
  lwip_init();
  Netif_Config();
  uint32_t oled_uptime_s = 0;
  uint32_t last_tick = HAL_GetTick();
  /* Separate, faster timer for the LAN status row -- see
   * ethernet_phy_debug_print() in ethernetif.c. */
  uint32_t phy_debug_timer = HAL_GetTick();

  CAMERA_STREAM_StatusTypeDef cam_status = CAMERA_STREAM_Init(&hi2c2, &hdcmipp, &hjpeg);
  if (cam_status != CAMERA_STREAM_OK)
  {
    /* Sensor not found or DCMIPP wouldn't start -- see
     * CAMERA_INTEGRATION.md's debugging checklist. Not calling
     * Error_Handler() here on purpose so Ethernet/OLED still come up
     * even if the camera doesn't. */
    if (oled_status == SH1106_OK)
    {
      /* Show the failing stage so the cause is visible without a debugger:
       *   NODEV = sensor didn't ACK / wrong chip-ID  (power, reset, I2C, FPC)
       *   INIT  = sensor ACKed but register init/start failed (reg table)
       *   DCMIPP= sensor OK but CSI/DCMIPP pipe wouldn't start */
      const char *msg;
      switch (cam_status)
      {
          case CAMERA_STREAM_ERROR_SENSOR_NOT_FOUND:
              msg = "CAM: NODEV";
              SH1106_SetCursor(0, 32);
              SH1106_WriteString(msg, SH1106_COLOR_WHITE);
              SH1106_UpdateScreen();
              BlinkBlue(2);
              break;

          case CAMERA_STREAM_ERROR_SENSOR_INIT:
              msg = "CAM: INIT";
              SH1106_SetCursor(0, 32);
              SH1106_WriteString(msg, SH1106_COLOR_WHITE);
              SH1106_UpdateScreen();
              BlinkBlue(3);
              break;

          case CAMERA_STREAM_ERROR_DCMIPP:
              msg = "CAM: DCMIPP";
              SH1106_SetCursor(0, 32);
              SH1106_WriteString(msg, SH1106_COLOR_WHITE);
              SH1106_UpdateScreen();
              BlinkBlue(4);
              break;

          default:
              msg = "CAM: FAIL";
              SH1106_SetCursor(0, 32);
              SH1106_WriteString(msg, SH1106_COLOR_WHITE);
              SH1106_UpdateScreen();
              BlinkBlue(5);
              break;
      }
      SH1106_SetCursor(0, 32);
      SH1106_WriteString(msg, SH1106_COLOR_WHITE);
      SH1106_UpdateScreen();
    }
  }
  else
  {
    MJPEG_SERVER_Init(80);
    for (uint8_t i = 0; i < 1; i++)
    {
        BSP_LED_On(LED_RED);
        HAL_Delay(150);
        BSP_LED_Off(LED_RED);
        HAL_Delay(150);
    }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* Service the network stack. NO_SYS/polling mode: these must be
     * called often (every loop iteration, not gated behind HAL_Delay)
     * or incoming frames back up in the RX descriptor ring and DHCP/ARP
     * timing gets sluggish. */
    ethernetif_input(&gnetif);
    sys_check_timeouts();
#if LWIP_NETIF_LINK_CALLBACK
    Ethernet_Link_Periodic_Handle(&gnetif);
#endif
#if LWIP_DHCP
    DHCP_Periodic_Handle(&gnetif);
#endif

    /* Same story as the lwIP calls above: cheap no-ops when there's
     * nothing to do, but need to run every iteration. */
    CAMERA_STREAM_Process();
    MJPEG_SERVER_Poll();

    /* Refresh the LAN connection status row every 200ms. */
    if (oled_status == SH1106_OK && (HAL_GetTick() - phy_debug_timer) >= 200)
    {
      phy_debug_timer = HAL_GetTick();
      ethernet_phy_debug_print();
    }

    if (oled_status == SH1106_OK && (HAL_GetTick() - last_tick) >= 1000)
    {
      last_tick = HAL_GetTick();
      oled_uptime_s++;

      /* Camera bring-up telemetry, two rows -- while CAMERA_STREAM's CSI
       * auto-scan (camera_stream.c) is walking combinations looking for a
       * lock, these rows show its progress instead; see
       * CAMERA_STREAM_GetCSIScanStatus()/CAMERA_STREAM_GetDebugCounts(). */
      CAMERA_STREAM_CSIScanStatusTypeDef scan = CAMERA_STREAM_GetCSIScanStatus();
      char line[17];

      if (scan.active)
      {
        snprintf(line, sizeof(line), "Scan %lu/%lu",
                 (unsigned long)scan.step, (unsigned long)scan.total);
        SH1106_FillRectangle(0, 48, SH1106_WIDTH - 1, 48 + FONT_HEIGHT - 1, SH1106_COLOR_BLACK);
        SH1106_SetCursor(0, 48);
        SH1106_WriteString(line, SH1106_COLOR_WHITE);

        snprintf(line, sizeof(line), "T:%luMb %luL%c",
                 (unsigned long)scan.mbps, (unsigned long)scan.lanes,
                 scan.inverted ? 'I' : 'P');
        SH1106_FillRectangle(0, 56, SH1106_WIDTH - 1, 56 + FONT_HEIGHT - 1, SH1106_COLOR_BLACK);
        SH1106_SetCursor(0, 56);
        SH1106_WriteString(line, SH1106_COLOR_WHITE);
      }
      else
      {
        /* Scan finished (or the current config already worked at step 0,
         * which looks the same from here): v climbs = sensor is
         * streaming; c climbs = capture OK. Row 56 becomes whichever
         * combination the scan landed on -- copy it into
         * CAM_CSI_PHY_BITRATE/CAM_CSI_LANE_MAPPING (above) so future
         * boots lock on step 0 again instead of re-scanning -- or, if the
         * full sweep found nothing, that Er was never a config problem. */
        uint32_t cap = 0, enc = 0, vs = 0, er = 0;
        CAMERA_STREAM_GetDebugCounts(&cap, &enc, &vs, &er);

        snprintf(line, sizeof(line), "v:%lu c:%lu",
                 (unsigned long)vs, (unsigned long)cap);
        SH1106_FillRectangle(0, 48, SH1106_WIDTH - 1, 48 + FONT_HEIGHT - 1, SH1106_COLOR_BLACK);
        SH1106_SetCursor(0, 48);
        SH1106_WriteString(line, SH1106_COLOR_WHITE);

        if (scan.locked)
        {
          snprintf(line, sizeof(line), "LK:%luMb %luL%c",
                   (unsigned long)scan.mbps, (unsigned long)scan.lanes,
                   scan.inverted ? 'I' : 'P');
        }
        else
        {
          snprintf(line, sizeof(line), "NO LOCK (chk HW)");
        }
        SH1106_FillRectangle(0, 56, SH1106_WIDTH - 1, 56 + FONT_HEIGHT - 1, SH1106_COLOR_BLACK);
        SH1106_SetCursor(0, 56);
        SH1106_WriteString(line, SH1106_COLOR_WHITE);
        (void)enc; (void)er;
      }

      (void)oled_uptime_s;
      SH1106_UpdateScreen();
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief DCMIPP Initialization Function
  * @param None
  * @retval None
  */
static void MX_DCMIPP_Init(void)
{

  /* USER CODE BEGIN DCMIPP_Init 0 */

  /* USER CODE END DCMIPP_Init 0 */

  DCMIPP_CSI_PIPE_ConfTypeDef pCSI_PipeConfig = {0};
  DCMIPP_CSI_ConfTypeDef pCSI_Config = {0};
  DCMIPP_PipeConfTypeDef pPipeConfig = {0};

  /* USER CODE BEGIN DCMIPP_Init 1 */

  /* USER CODE END DCMIPP_Init 1 */
  hdcmipp.Instance = DCMIPP;
  if (HAL_DCMIPP_Init(&hdcmipp) != HAL_OK)
  {
    Error_Handler();
  }

  /** Pipe 1 Config
  */
  pCSI_PipeConfig.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  pCSI_PipeConfig.DataTypeIDA = DCMIPP_DT_RAW8;
  pCSI_PipeConfig.DataTypeIDB = DCMIPP_DT_RAW8;
  if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1, &pCSI_PipeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  pCSI_Config.PHYBitrate = DCMIPP_CSI_PHY_BT_80;
  pCSI_Config.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  pCSI_Config.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  HAL_DCMIPP_CSI_SetConfig(&hdcmipp, &pCSI_Config);
  pPipeConfig.FrameRate = DCMIPP_FRAME_RATE_ALL;
  pPipeConfig.PixelPipePitch = 1280;
  pPipeConfig.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
  if (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1, &pPipeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp, 0U, DCMIPP_CSI_DT_BPP8) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DCMIPP_Init 2 */

  /* Lanes/data type/bpp/pitch are all fixed at the source now (checked
   * against RedEye.ioc) -- the four-line override that used to be here
   * for those is gone, re-asserting values that now have a real source
   * of truth above would just make it harder to notice if CubeMX ever
   * disagrees with itself again. This one field still doesn't have a
   * verified value anywhere though: */
  /* CSI D-PHY receive bit-rate. This MUST match the rate the OV5647
   * actually drives on its MIPI lanes, or the PHY never locks and
   * HAL_DCMIPP_PIPE_FrameEventCallback never fires (symptom: sensor
   * probes fine but v:0 c:0 -- no frame ever starts).
   *
   * Computed from the OV5647 PLL1 tree (same block as OV5640), NOT guessed:
   *   bit_rate/lane = XVCLK * mult / pre_div / sys_div / mipi_div
   * with XVCLK=25 MHz and the ov5647.c register table:
   *   0x3036=0x46 -> mult=70
   *   0x3037 unwritten -> reset default 0x03 -> pre_div=3
   *   0x3035=0x21 -> sys_div=2, mipi_div=1   (0x21 IS the correct mainline
   *                  VGA value; it's written twice in the kernel's
   *                  ov5647_640x480 table and the last write, 0x21, wins)
   *   0x3034=0x08 -> 8-bit; sits on the SCLK branch, not the serial branch,
   *                  so it does NOT affect the lane rate.
   * => 25*70/3/2/1 = 291.67 Mbit/s per lane (link/clock-lane 145.83 MHz).
   * Matches the mainline ov5647 VGA link_freq of 145,833,300 Hz exactly.
   *
   * The DCMIPP_CSI_PHY_BT_* bands are upper bounds; 291.67 sits between
   * _275 and _300, so BT_300 is the correct (tightest) band. The old
   * BT_600 assumed sys_div=1 (583 Mbit/s) and was 2x too high, so the
   * D-PHY never locked -- that was the root cause of v:0 c:0. If frames
   * still don't land with the CSI error IRQ now wired, watch Er: Er>0
   * means bracket +/- one band (BT_275 / BT_325) or the lane order/
   * polarity is swapped in the MB1723 adapter; Er==0 means no signal at
   * all (sensor not streaming / clock lane / FPC).
   *
   * FIX: this used to hardcode DCMIPP_CSI_PHY_BT_300 and never touch
   * DataLaneMapping again after its CubeMX-generated default, so the
   * CAM_CSI_PHY_BITRATE / CAM_CSI_LANE_MAPPING knobs above this function
   * were dead -- edit/rebuild/reflash changed nothing on the wire. Both
   * now actually feed the struct that gets applied here. */
  pCSI_Config.PHYBitrate = CAM_CSI_PHY_BITRATE;
  pCSI_Config.DataLaneMapping = CAM_CSI_LANE_MAPPING;
  if (HAL_DCMIPP_CSI_SetConfig(&hdcmipp, &pCSI_Config) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE END DCMIPP_Init 2 */

}

/**
  * @brief ETH1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH1_Init(void)
{

  /* USER CODE BEGIN ETH1_Init 0 */

  /* USER CODE END ETH1_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH1_Init 1 */

  /* USER CODE END ETH1_Init 1 */
  heth1.Instance = ETH1;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth1.Init.MACAddr = &MACAddr[0];
  heth1.Init.MediaInterface = HAL_ETH_RMII_MODE;
  for (int ch = 0; ch < ETH_DMA_CH_CNT; ch++)
  {
    heth1.Init.TxDesc[ch] = DMATxDscrTab[ch];
    heth1.Init.RxDesc[ch] = DMARxDscrTab[ch];
  }
  heth1.Init.RxBuffLen = 1536;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth1) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE BEGIN ETH1_Init 2 */

  /* USER CODE END ETH1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x30C0EDFF;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x30C0EDFF;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief JPEG Initialization Function
  * @param None
  * @retval None
  */
static void MX_JPEG_Init(void)
{

  /* USER CODE BEGIN JPEG_Init 0 */

  /* USER CODE END JPEG_Init 0 */

  /* USER CODE BEGIN JPEG_Init 1 */

  /* USER CODE END JPEG_Init 1 */
  hjpeg.Instance = JPEG;
  if (HAL_JPEG_Init(&hjpeg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN JPEG_Init 2 */

  /* USER CODE END JPEG_Init 2 */

}

/**
  * @brief RIF Initialization Function
  * @param None
  * @retval None
  */
  static void SystemIsolation_Config(void)
{

  /* USER CODE BEGIN RIF_Init 0 */

  /* USER CODE END RIF_Init 0 */

  /* set all required IPs as secure privileged */
  __HAL_RCC_RIFSC_CLK_ENABLE();

  /*RIMC configuration*/
  RIMC_MasterConfig_t RIMC_master = {0};
  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);

  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_NPRIV;
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_ETH1, &RIMC_master);

  /*RISUP configuration*/
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_JPEG , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

  /* RIF-Aware IPs Config */

  /* set up GPIO configuration */
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_7,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOH,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);

  /* USER CODE BEGIN RIF_Init 1 */

  /* CubeMX regenerates the block above with RIF_ATTRIBUTE_NPRIV and no
   * slave-side call every time "Generate Code" runs -- that's what
   * silently happened here. Re-applying both fixes inside this USER
   * CODE block means they survive the next regeneration instead of
   * quietly disappearing again.
   *
   * (1) ETH1 DMA master needs PRIVILEGED, not NPRIV: with NPRIV, ETH
   * DMA transactions can be silently fenced by the RIF security fabric
   * -- HAL calls report success and the TX descriptor's OWN bit gets
   * set by software, but the actual DMA bus transaction to fetch/
   * consume that descriptor never happens, so nothing reaches the PHY.
   */
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_ETH1, &RIMC_master);

  /* (2) ETH1's own (slave-side) RIF security attribute is never set by
   * the generated block above -- only the master (RIMC) side and the
   * GPIO pins are. Left at its reset default, it can end up mismatched
   * against the master attribute and the Secure memory the DMA
   * descriptors/buffers live in (see the 0x34100000 placement of
   * DMARxDscrTab/DMATxDscrTab), which is exactly the kind of gap that
   * causes DMA to silently stall instead of erroring out. */
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_ETH1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_NPRIV);

  /* DCMIPP's override that used to live here is gone -- CubeMX now
   * generates a complete, correct native grant for it above (master
   * *and* slave, both PRIV; RIF.RISUP.DCMIPP.Privilege=true in the
   * ioc), matching JPEG's. The old override was actively wrong by
   * the end: it re-set the slave attribute to NPRIV right after the
   * generated code above had just set it to the correct PRIV,
   * silently undoing it every build. */

  /* (3) CSI has NO grant anywhere -- not in the generated block above,
   * not in RedEye.ioc (grep confirms no RIF.RISUP.CSI.* key exists at
   * all, unlike DCMIPP's RIF.RISUP.DCMIPP.Privilege=true), and not
   * previously here either. RIF_RISC_PERIPH_INDEX_CSI is a real, separate
   * RISC slave resource (stm32n6xx_hal_rif.h: SEC28, immediately next to
   * DCMIPP's SEC29 in the same register) -- CSI just never got the grant
   * DCMIPP and JPEG did, so it's been sitting at whatever the silicon
   * reset default is this whole time.
   *
   * This is the same failure class as (1)/(2) above, and it fits the
   * symptom that sent us hunting for it exactly: HAL_DCMIPP_CSI_SetConfig()
   * writes CR/PCR/PRCR/PFCR/PTCR0 and the D-PHY test-interface registers
   * (DCMIPP_CSI_WritePHYReg) straight into the CSI peripheral. Every one
   * of those calls has returned HAL_OK throughout every round of this
   * bring-up -- the osc_freq_target fix, the IC18 clock fix, all 253
   * PHYBitrate x DataLaneMapping x NumberOfLanes combinations in the
   * auto-scan -- with zero effect on real hardware behavior. A RIF
   * mismatch here means none of those writes were ever necessarily
   * reaching the actual registers: the HAL doesn't (can't) distinguish
   * "wrote the register" from "the write was silently fenced by RIF",
   * so HAL_OK doesn't mean what it looks like it means. That would
   * explain every one of the last three rounds coming up empty despite
   * being individually well-reasoned and independently verified. */
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

  /* USER CODE END RIF_Init 1 */
  /* USER CODE BEGIN RIF_Init 2 */

  /* USER CODE END RIF_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief  Bring up the LwIP network interface on top of heth1 / ethernetif.
  * @retval None
  */
static void Netif_Config(void)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;

#if LWIP_DHCP
  ip_addr_set_zero_ip4(&ipaddr);
  ip_addr_set_zero_ip4(&netmask);
  ip_addr_set_zero_ip4(&gw);
#else
  IP_ADDR4(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
  IP_ADDR4(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
  IP_ADDR4(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
#endif /* LWIP_DHCP */

  /* Add the network interface: ethernetif_init() brings up heth1,
   * ethernet_input() is LwIP's standard Ethernet-frame demux. */
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

  netif_set_default(&gnetif);

  if (netif_is_link_up(&gnetif))
  {
    netif_set_up(&gnetif);
  }
  else
  {
    netif_set_down(&gnetif);
  }

  /* Drives DHCP_state / OLED status updates in app_ethernet.c whenever
   * the link (or DHCP config) changes. */
  netif_set_link_callback(&gnetif, ethernet_link_status_updated);

  /* netif_add() -> ethernetif_init() -> low_level_init() already ran
   * ethernet_link_check_state() above, *before* this callback existed.
   * If the cable was plugged in before power-up, the PHY link is up by
   * the time we get here, so netif_set_link_up() already fired-and-was-
   * ignored (lwIP only calls the callback on an up/down *transition*,
   * and only if a callback is registered at that moment). Later, the
   * 100ms periodic check in ethernet_link_check_state() also does
   * nothing, because as far as it's concerned no transition occurs --
   * link was already up last time it looked. Net result: the OLED gets
   * stuck on "ETH: init..." forever and DHCP never starts.
   *
   * Fix: manually sync the display/DHCP state machine to whatever the
   * link state actually is right now, instead of waiting for an edge
   * that may have already been missed. */
  ethernet_link_status_updated(&gnetif);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
