
/**
  ******************************************************************************
  * @file    app_x-cube-ai.c
  * @author  X-CUBE-AI C code generator
  * @brief   AI program body
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

  /**
    * Description
    * Minimum template to show how to use the Neural-ART Embedded Client API
    *          Re-target of the printf function is out-of-scope.
    *
    *
    */

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/


/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "app_x-cube-ai.h"

#include "stai.h"
#include "npu_init.h"

/* USER CODE BEGIN includes */
#include "jpeg_utils.h"
#include "frame_source.h"
#include "sh1106.h"
#include "vnd_classes.h"

extern JPEG_HandleTypeDef hjpeg;   /* declared in main.c, MX_JPEG_Init() must run before AI init */
extern I2C_HandleTypeDef  hi2c1;   /* SH1106 OLED, already brought up by main.c */

/* Below this raw sigmoid-output score, we treat a detection as "nothing
 * confident enough" rather than guessing. Tune by watching real readings
 * on a few known bills; there's no single right number for every camera/
 * lighting setup. */
#define VND_CONF_THRESHOLD   0.55f

/* Model input is fixed at 320x320x3 (see best_int8.onnx) -- matches what
 * the Pi already sends, so no resize step is needed here. */
#define AI_IMG_SIZE          320U
#define AI_IMG_PIXELS        (AI_IMG_SIZE * AI_IMG_SIZE)

/* Decoded RGB565 landing buffer for one frame (320*320*2 = 200KB). Plain
 * .bss -- with RAM now capped at 1MB (see the linker script) so as not to
 * collide with the NPU's own pools, this is the one sizeable new chunk of
 * RAM this integration adds. Worth checking the .map file after a first
 * build to confirm it and the rest of the app's .bss actually fit. */
static uint8_t ai_rgb565_buf[AI_IMG_PIXELS * 2U];

/* ---- JPEG decode (interrupt-driven HAL_JPEG + jpeg_utils color convert) - */
static volatile uint8_t  jpeg_decode_done;
static volatile uint8_t  jpeg_decode_error;
static JPEG_YCbCrToRGB_Convert_Function pJpegColorConvertFunc;
static uint32_t jpeg_mcu_total_nb;
static uint32_t jpeg_mcu_block_index;

void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef *hjpeg_, JPEG_ConfTypeDef *pInfo)
{
  uint32_t mcus_per_call = 0;
  JPEG_GetDecodeColorConvertFunc(pInfo, &pJpegColorConvertFunc, &mcus_per_call);
  jpeg_mcu_total_nb   = mcus_per_call;
  jpeg_mcu_block_index = 0;
  /* We only ever feed this decoder frames captured at 320x320 on the Pi
   * side, so pInfo->ImageWidth/ImageHeight aren't cross-checked here --
   * if you ever point this at a different source, add a size check and
   * bail out (jpeg_decode_error = 1) rather than writing past
   * ai_rgb565_buf. */
}

/* We hand the whole compressed frame to HAL_JPEG_Decode_IT() up front
 * (FRAME_SOURCE already gave us one complete, contiguous JPEG payload),
 * so there's never more input to hand over mid-decode. */
void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg_, uint32_t NbDecodedData)
{
  HAL_JPEG_Pause(hjpeg_, JPEG_PAUSE_RESUME_INPUT);
}

void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg_,
                                uint8_t *pDataOut,
                                uint32_t OutDataLength)
{
    uint32_t ConvertedDataCount = 0;

    if (pJpegColorConvertFunc != NULL)
    {
        pJpegColorConvertFunc(
            pDataOut,
            &ai_rgb565_buf[jpeg_mcu_block_index],
            jpeg_mcu_block_index,
            OutDataLength,
            &ConvertedDataCount
        );

        jpeg_mcu_block_index += ConvertedDataCount;
    }

    HAL_JPEG_ConfigOutputBuffer(hjpeg_, pDataOut, OutDataLength);
}

void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hjpeg_)
{
  jpeg_decode_done = 1;
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg_)
{
  jpeg_decode_error = 1;
  jpeg_decode_done = 1;
}

/* Track our own place in the frame stream, separately from
 * mjpeg_server.c's tracking -- FRAME_SOURCE is built to support exactly
 * this, multiple independent readers of "the latest frame". */
static uint32_t ai_last_frame_id = 0;

/* USER CODE END includes */

/* IO buffers ----------------------------------------------------------------*/


/* Input defs ----------------------------------------------------------------*/
#include "aiTestUtility.h"
/**

// Array to store the data of the input tensor
stai_ptr data_ins[] = {
}; 
*/

/* Output defs ----------------------------------------------------------------*/

/**

// c-array to store the data of the output tensor
stai_ptr data_outs[] = {
}; 
*/




/* Global byte buffer to save instantiated C-model network context */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)

/* Activations buffers -------------------------------------------------------*/




/* Entry points --------------------------------------------------------------*/

/* Array of pointer to manage the model's input/output tensors */
static stai_size in_length, out_length;
static stai_ptr stai_input[STAI_NETWORK_IN_NUM];
static stai_ptr stai_output[STAI_NETWORK_OUT_NUM];


/* 
 * Bootstrap
 */
int aiInit(void) {
  stai_return_code ret_code;
  
  /* 1: Initialize runtime library */
  ret_code = stai_runtime_init();
  
  /* 2: Initialize network model context */
  ret_code = stai_network_init(network_context);
  ret_code = stai_network_get_inputs(network_context, stai_input, &in_length);
  
  ret_code = stai_network_get_outputs(network_context, stai_output, &out_length);
  
  return 0;
}

int aiDeinit(void) {
  stai_return_code ret_code;

  /* 1: Deinitialize network model context */
  ret_code = stai_network_deinit(network_context);
  
  /* 2: Deinitialize runtime library */
  ret_code = stai_runtime_deinit();

  return 0;
}

/* 
 * Run inference
 */
stai_return_code aiRun() {
  stai_return_code ret_code;

  ret_code = stai_network_run(network_context, STAI_MODE_SYNC);
  if (ret_code != STAI_SUCCESS) {
      ret_code = stai_network_get_error(network_context);
  }
  return ret_code;
}


/* Returns 1 if a frame was found, decoded, and copied into the network's
 * input buffer; 0 if there was no new frame to process (not an error --
 * just means AI_ProcessLatestFrame() has nothing to do this call). */
int acquire_and_process_data()
{
  /* USER CODE BEGIN acquire_and_process_data */
  uint8_t  *jpeg_data = NULL;
  uint32_t  jpeg_len = 0, new_frame_id = 0;
  uint32_t  i;
  int8_t   *in_r, *in_g, *in_b;

  if (!FRAME_SOURCE_GetLatestJPEG(ai_last_frame_id, &jpeg_data, &jpeg_len, &new_frame_id))
  {
    return 0;   /* nothing newer than what we already processed */
  }

  jpeg_decode_done  = 0;
  jpeg_decode_error = 0;
  pJpegColorConvertFunc = NULL;

  if (HAL_JPEG_Decode_IT(&hjpeg, jpeg_data, jpeg_len, ai_rgb565_buf, sizeof(ai_rgb565_buf)) != HAL_OK)
  {
    FRAME_SOURCE_ReleaseJPEG(jpeg_data);
    return 0;
  }

  /* Simple, synchronous wait -- fine as long as AI_ProcessLatestFrame() is
   * called from the main loop rather than an ISR. 320x320 decode is a few
   * ms; 200ms is a generous ceiling in case a frame is malformed. */
  {
    uint32_t start = HAL_GetTick();
    while (!jpeg_decode_done && (HAL_GetTick() - start) < 200U)
    {
      /* busy-wait for the JPEG ISR chain to finish */
    }
  }

  FRAME_SOURCE_ReleaseJPEG(jpeg_data);
  ai_last_frame_id = new_frame_id;

  if (!jpeg_decode_done || jpeg_decode_error)
  {
    return 0;
  }

  /* RGB565 -> per-channel int8, NCHW layout, in one pass. Quantization here
   * is QLinear(scale=1/255, zero_point=-128) per the model's input tensor
   * (best_int8.onnx / stedgeai report) -- for a pixel value already in
   * 0..255, that's just "value - 128" with no floating point needed. If
   * you ever swap in a model quantized differently, redo this as
   * round(pixel/255.0f/scale) + zero_point using that model's actual
   * scale/zero-point instead of assuming this shortcut still applies. */
  in_r = (int8_t *)stai_input[0];
  in_g = in_r + AI_IMG_PIXELS;
  in_b = in_g + AI_IMG_PIXELS;

  for (i = 0; i < AI_IMG_PIXELS; i++)
  {
    uint16_t px = ((uint16_t *)ai_rgb565_buf)[i];
    uint8_t r5 = (px >> 11) & 0x1F;
    uint8_t g6 = (px >> 5)  & 0x3F;
    uint8_t b5 =  px        & 0x1F;
    uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
    uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
    uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));

    in_r[i] = (int8_t)(r8 - 128);
    in_g[i] = (int8_t)(g8 - 128);
    in_b[i] = (int8_t)(b8 - 128);
  }

  return 1;
  /* USER CODE END acquire_and_process_data */
}

/* Reads the [1,13,2100] output tensor (4 box coords + 9 class scores, per
 * anchor, already through a final sigmoid per the generated network), picks
 * the single most confident class across every anchor, and shows it on the
 * OLED if it clears VND_CONF_THRESHOLD. We only care about "what bill is
 * this" here, not "where in frame" -- so this deliberately skips decoding
 * the box coordinates and skips NMS. If you later want to report more than
 * one bill at a time, this is the place to add both back in. */
int post_process()
{
  /* USER CODE BEGIN post_process */
  const float *out = (const float *)stai_output[0];
  const uint32_t n_anchors = 2100U;
  const uint32_t n_classes = VND_NUM_CLASSES;   /* 9 */

  float   best_score = 0.0f;
  int32_t best_class = -1;
  uint32_t a, c;

  for (a = 0; a < n_anchors; a++)
  {
    for (c = 0; c < n_classes; c++)
    {
      float score = out[(4U + c) * n_anchors + a];
      if (score > best_score)
      {
        best_score = score;
        best_class = (int32_t)c;
      }
    }
  }

  SH1106_Fill(SH1106_COLOR_BLACK);
  SH1106_SetCursor(0, 0);
  if (best_class >= 0 && best_score >= VND_CONF_THRESHOLD)
  {
    char line[24];
    SH1106_WriteString("Detected:", SH1106_COLOR_WHITE);
    SH1106_SetCursor(0, 16);
    snprintf(line, sizeof(line), "%s VND", vnd_class_names[best_class]);
    SH1106_WriteString(line, SH1106_COLOR_WHITE);
  }
  else
  {
    SH1106_WriteString("No bill detected", SH1106_COLOR_WHITE);
  }
  SH1106_UpdateScreen();

  return 0;
  /* USER CODE END post_process */
}


/* Called once per iteration of the existing main loop (see main.c). Does
 * nothing if no new camera frame has arrived since the last call -- so
 * it's safe to call unconditionally rather than gating it on a timer. */
void AI_ProcessLatestFrame(void)
{
  /* USER CODE BEGIN main_loop */
  if (acquire_and_process_data())
  {
    if (aiRun() == STAI_SUCCESS)
    {
      post_process();
    }
  }
  /* USER CODE END main_loop */
}


/* Entry points --------------------------------------------------------------*/



void STM32CubeAI_Studio_AI_Init(void)
{
    aiPreInitialize();
    aiInit();  
    /* USER CODE BEGIN init */
    /* USER CODE END init */
}

void STM32CubeAI_Studio_AI_Process(void)
{
    AI_ProcessLatestFrame();
} 

void STM32CubeAI_Studio_AI_Deinit(void)
{
    aiDeinit();
} 


#ifdef __cplusplus
}
#endif
