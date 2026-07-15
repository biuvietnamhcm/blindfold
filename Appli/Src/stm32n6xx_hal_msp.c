/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file         stm32n6xx_hal_msp.c
  * @brief        This file provides code for the MSP Initialization
  *               and de-Initialization codes.
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
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
/**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{

  /* USER CODE BEGIN MspInit 0 */

  /* USER CODE END MspInit 0 */

  /* System interrupt init*/

  HAL_PWREx_EnableVddIO2();

  HAL_PWREx_EnableVddIO3();

  HAL_PWREx_EnableVddIO4();

  HAL_PWREx_EnableVddIO5();

  /* USER CODE BEGIN MspInit 1 */

  /* USER CODE END MspInit 1 */
}

/**
  * @brief DCMIPP MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hdcmipp: DCMIPP handle pointer
  * @retval None
  */
void HAL_DCMIPP_MspInit(DCMIPP_HandleTypeDef* hdcmipp)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hdcmipp->Instance==DCMIPP)
  {
    /* USER CODE BEGIN DCMIPP_MspInit 0 */

    /* USER CODE END DCMIPP_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP|RCC_PERIPHCLK_CSI;
    PeriphClkInitStruct.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_PCLK5;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL4;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockDivider = 1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_DCMIPP_CLK_ENABLE();

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**DCMIPP GPIO Configuration
    PE8     ------> DCMIPP_D4
    PC6     ------> DCMIPP_D1
    PC5     ------> DCMIPP_D2
    PE10     ------> DCMIPP_D3
    PE4     ------> DCMIPP_D5
    PD5     ------> DCMIPP_PIXCLK
    PD7     ------> DCMIPP_D0
    PF1     ------> DCMIPP_D7
    PG2     ------> DCMIPP_D6
    */
    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_10|GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF9_DCMIPP;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF9_DCMIPP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF9_DCMIPP;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF9_DCMIPP;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF9_DCMIPP;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* USER CODE BEGIN DCMIPP_MspInit 1 */

    /* USER CODE END DCMIPP_MspInit 1 */

  }

}

/**
  * @brief DCMIPP MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hdcmipp: DCMIPP handle pointer
  * @retval None
  */
void HAL_DCMIPP_MspDeInit(DCMIPP_HandleTypeDef* hdcmipp)
{
  if(hdcmipp->Instance==DCMIPP)
  {
    /* USER CODE BEGIN DCMIPP_MspDeInit 0 */

    /* USER CODE END DCMIPP_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_DCMIPP_CLK_DISABLE();

    /**DCMIPP GPIO Configuration
    PE8     ------> DCMIPP_D4
    PC6     ------> DCMIPP_D1
    PC5     ------> DCMIPP_D2
    PE10     ------> DCMIPP_D3
    PE4     ------> DCMIPP_D5
    PD5     ------> DCMIPP_PIXCLK
    PD7     ------> DCMIPP_D0
    PF1     ------> DCMIPP_D7
    PG2     ------> DCMIPP_D6
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_8|GPIO_PIN_10|GPIO_PIN_4);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6|GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_5|GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_1);

    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_2);

    /* USER CODE BEGIN DCMIPP_MspDeInit 1 */

    /* USER CODE END DCMIPP_MspDeInit 1 */
  }

}

/**
  * @brief ETH MSP Initialization
  * This function configures the hardware resources used in this example
  * @param heth: ETH handle pointer
  * @retval None
  */
void HAL_ETH_MspInit(ETH_HandleTypeDef* heth)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(heth->Instance==ETH1)
  {
    /* USER CODE BEGIN ETH1_MspInit 0 */

    /* USER CODE END ETH1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ETH1;
    PeriphClkInitStruct.Eth1ClockSelection = RCC_ETH1CLKSOURCE_HCLK;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_ETH1_CLK_ENABLE();
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();

    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**ETH1 GPIO Configuration
    PF4     ------> ETH1_MDIO
    PF10     ------> ETH1_RMII_CRS_DV
    PF7     ------> ETH1_RMII_REF_CLK
    PF5     ------> ETH1_CLK
    PF15     ------> ETH1_RMII_RXD1
    PF14     ------> ETH1_RMII_RXD0
    PF11     ------> ETH1_RMII_TX_EN
    PF13     ------> ETH1_RMII_TXD1
    PF12     ------> ETH1_RMII_TXD0
    PG11     ------> ETH1_MDC
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_10|GPIO_PIN_7|GPIO_PIN_5
                          |GPIO_PIN_15|GPIO_PIN_14|GPIO_PIN_11|GPIO_PIN_13
                          |GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH1;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH1;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* USER CODE BEGIN ETH1_MspInit 1 */

    /* CubeMX's generator for this STM32N6/NUCLEO-N657X0-Q combo does not
     * emit a GPIOG block for ETH1_MDC (PG11) here, even though the .ioc
     * has PG11.Signal=ETH1_MDC set -- confirmed dropped across at least
     * one prior "Generate Code" run. Kept inside this USER CODE block
     * (rather than loose in the auto-generated section above) so it
     * survives every future regeneration instead of silently
     * disappearing again.
     *
     * AF11, not AF12: same alternate function as every other RMII/MDIO
     * pin on GPIOF above. Confirmed against ST's own CubeMX-generated
     * msp for this exact board (STM32CubeN6, Projects/NUCLEO-N657X0-Q/
     * Applications/NetXDuo/Nx_TCP_Echo_Client) and against a third-party
     * STM32N6 driver targeting NUCLEO-N657X0-Q (Oryx Embedded
     * CycloneTCP). AF12 was the earlier guess and left MDC undriven:
     * with no valid clock reaching the LAN8742, every MDIO read comes
     * back as the all-1s non-responding pattern
     * (ethernet_phy_is_responding() correctly refuses to trust that as
     * a real link state), so the link could only ever report DOWN no
     * matter what was plugged into the RJ45 jack.
     *
     * If a future CubeMX/CubeIDE version starts generating this block
     * correctly on its own, this USER CODE copy will just be redundant
     * (harmless -- HAL_GPIO_Init on the same pin twice with the same
     * settings), not conflicting. */
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH1;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* USER CODE END ETH1_MspInit 1 */

  }

}

/**
  * @brief ETH MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param heth: ETH handle pointer
  * @retval None
  */
void HAL_ETH_MspDeInit(ETH_HandleTypeDef* heth)
{
  if(heth->Instance==ETH1)
  {
    /* USER CODE BEGIN ETH1_MspDeInit 0 */

    /* USER CODE END ETH1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ETH1_CLK_DISABLE();
    __HAL_RCC_ETH1MAC_CLK_DISABLE();
    __HAL_RCC_ETH1TX_CLK_DISABLE();
    __HAL_RCC_ETH1RX_CLK_DISABLE();

    /**ETH1 GPIO Configuration
    PF4     ------> ETH1_MDIO
    PF10     ------> ETH1_RMII_CRS_DV
    PF7     ------> ETH1_RMII_REF_CLK
    PF5     ------> ETH1_CLK
    PF15     ------> ETH1_RMII_RXD1
    PF14     ------> ETH1_RMII_RXD0
    PF11     ------> ETH1_RMII_TX_EN
    PF13     ------> ETH1_RMII_TXD1
    PF12     ------> ETH1_RMII_TXD0
    PG11     ------> ETH1_MDC
    */
    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_4|GPIO_PIN_10|GPIO_PIN_7|GPIO_PIN_5
                          |GPIO_PIN_15|GPIO_PIN_14|GPIO_PIN_11|GPIO_PIN_13
                          |GPIO_PIN_12);

    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11);

    /* USER CODE BEGIN ETH1_MspDeInit 1 */

    /* Counterpart of the USER CODE block in HAL_ETH_MspInit() above --
     * PG11 (ETH1_MDC) de-init kept here for the same reason: CubeMX's
     * generator doesn't reliably emit this GPIOG line on its own, so it
     * lives in USER CODE instead of the auto-generated section. */
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11);

    /* USER CODE END ETH1_MspDeInit 1 */
  }

}

/**
  * @brief I2C MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hi2c: I2C handle pointer
  * @retval None
  */
void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hi2c->Instance==I2C1)
  {
    /* USER CODE BEGIN I2C1_MspInit 0 */

    /* USER CODE END I2C1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    PeriphClkInitStruct.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    /**I2C1 GPIO Configuration
    PC1     ------> I2C1_SDA
    PH9     ------> I2C1_SCL
    */
    GPIO_InitStruct.Pin = I2C1_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(I2C1_SDA_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = I2CA_SCL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(I2CA_SCL_GPIO_Port, &GPIO_InitStruct);

    /* Peripheral clock enable */
    __HAL_RCC_I2C1_CLK_ENABLE();
    /* USER CODE BEGIN I2C1_MspInit 1 */

    /* USER CODE END I2C1_MspInit 1 */
  }
  else if(hi2c->Instance==I2C2)
  {
    /* USER CODE BEGIN I2C2_MspInit 0 */

    /* USER CODE END I2C2_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
    PeriphClkInitStruct.I2c2ClockSelection = RCC_I2C2CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2C2 GPIO Configuration
    PB11     ------> I2C2_SDA
    PB10     ------> I2C2_SCL
    */
    GPIO_InitStruct.Pin = I2C2_SDA_Pin|I2C2_SCL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Peripheral clock enable */
    __HAL_RCC_I2C2_CLK_ENABLE();
    /* USER CODE BEGIN I2C2_MspInit 1 */

    /* USER CODE END I2C2_MspInit 1 */
  }

}

/**
  * @brief I2C MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hi2c: I2C handle pointer
  * @retval None
  */
void HAL_I2C_MspDeInit(I2C_HandleTypeDef* hi2c)
{
  if(hi2c->Instance==I2C1)
  {
    /* USER CODE BEGIN I2C1_MspDeInit 0 */

    /* USER CODE END I2C1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PC1     ------> I2C1_SDA
    PH9     ------> I2C1_SCL
    */
    HAL_GPIO_DeInit(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin);

    HAL_GPIO_DeInit(I2CA_SCL_GPIO_Port, I2CA_SCL_Pin);

    /* USER CODE BEGIN I2C1_MspDeInit 1 */

    /* USER CODE END I2C1_MspDeInit 1 */
  }
  else if(hi2c->Instance==I2C2)
  {
    /* USER CODE BEGIN I2C2_MspDeInit 0 */

    /* USER CODE END I2C2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C2_CLK_DISABLE();

    /**I2C2 GPIO Configuration
    PB11     ------> I2C2_SDA
    PB10     ------> I2C2_SCL
    */
    HAL_GPIO_DeInit(I2C2_SDA_GPIO_Port, I2C2_SDA_Pin);

    HAL_GPIO_DeInit(I2C2_SCL_GPIO_Port, I2C2_SCL_Pin);

    /* USER CODE BEGIN I2C2_MspDeInit 1 */

    /* USER CODE END I2C2_MspDeInit 1 */
  }

}

/**
  * @brief JPEG MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hjpeg: JPEG handle pointer
  * @retval None
  */
void HAL_JPEG_MspInit(JPEG_HandleTypeDef* hjpeg)
{
  if(hjpeg->Instance==JPEG)
  {
    /* USER CODE BEGIN JPEG_MspInit 0 */

    /* USER CODE END JPEG_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_JPEG_CLK_ENABLE();
    /* USER CODE BEGIN JPEG_MspInit 1 */

    /* USER CODE END JPEG_MspInit 1 */

  }

}

/**
  * @brief JPEG MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hjpeg: JPEG handle pointer
  * @retval None
  */
void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef* hjpeg)
{
  if(hjpeg->Instance==JPEG)
  {
    /* USER CODE BEGIN JPEG_MspDeInit 0 */

    /* USER CODE END JPEG_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_JPEG_CLK_DISABLE();
    /* USER CODE BEGIN JPEG_MspDeInit 1 */

    /* USER CODE END JPEG_MspDeInit 1 */
  }

}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
