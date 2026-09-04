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
#include "ai_display.h"
#include "vnd_classes.h"

extern JPEG_HandleTypeDef hjpeg;   /* declared in main.c, MX_JPEG_Init() must run before AI init */
extern I2C_HandleTypeDef  hi2c1;   /* SH1106 OLED, already brought up by main.c */

/* Below this dequantized score, we treat a detection as "nothing
 * confident enough" rather than guessing. Tune by watching real readings
 * on a few known bills; there's no single right number for every camera/
 * lighting setup.
 *
 * NOTE: this now assumes the SPLIT-OUTPUT model
 * (best_int8_uint8io_split_outputs.onnx) rather than the original
 * best_int8_uint8io_OE_3_3_1.onnx. The original model concatenated box
 * coordinates (range ~0..320px) and class scores (range 0..1) into one
 * [1,13,2100] output tensor before its final int8 quantization step, which
 * forced both branches to share a single scale sized for the box-coordinate
 * range. That left only ~1 usable bit of resolution for the class scores --
 * confirmed by inspecting the ONNX graph and running it: a raw sigmoid
 * confidence had to exceed ~90% before the dequantized score moved off
 * exactly 0.0, so VND_CONF_THRESHOLD could never discriminate real-world
 * confidence levels no matter how it was tuned.
 *
 * The split-output model exposes the class-score branch as its own output
 * tensor, cut from the graph BEFORE that shared final quantize step, at the
 * point where it already had its own well-calibrated per-tensor scale
 * (confirmed: 0.00390625 = 1/256, i.e. full 256-level resolution across
 * 0..1, vs. 2 usable levels before). Box coordinates are untouched --
 * verified bit-identical to the original model's box output on the same
 * input -- they just live in their own tensor now instead of sharing one
 * with the class scores. Re-validate VND_CONF_THRESHOLD against real bills
 * once this is flashed; 0.55 is a reasonable starting point now that the
 * underlying data can actually represent it, but it was never meaningfully
 * tested against the old model's broken resolution. */
#define VND_CONF_THRESHOLD   0.55f

/* Model input is fixed at 320x320x3 (see
 * best_int8_uint8io_split_outputs.onnx, derived from run-12's
 * best_int8_uint8io_OE_3_3_1.onnx) -- matches what the Pi already sends,
 * so no resize step is needed here. */
#define AI_IMG_SIZE          320U
#define AI_IMG_PIXELS        (AI_IMG_SIZE * AI_IMG_SIZE)

/* Decoded RGB565 landing buffer for one frame (320*320*2 = 200KB). Plain
 * .bss -- with RAM now capped at 1MB (see the linker script) so as not to
 * collide with the NPU's own pools, this is the one sizeable new chunk of
 * RAM this integration adds. Worth checking the .map file after a first
 * build to confirm it and the rest of the app's .bss actually fit. */
static uint8_t ai_rgb565_buf[AI_IMG_PIXELS * 2U];

/* Raw, still-YCbCr MCU landing buffer -- what the JPEG hardware itself
 * writes into as it decodes, BEFORE jpeg_utils.c's color-convert function
 * turns it into RGB565. This must be a buffer distinct from ai_rgb565_buf:
 * jpeg_utils.c's *_ARGB_ConvertBlocks() helpers walk pInBuffer (this
 * buffer) sequentially by raw MCU size while writing pOutBuffer
 * (ai_rgb565_buf) at absolute pixel offsets -- for 4:2:0 source data one
 * MCU is 384 raw bytes in but 512 RGB565 bytes out, so if both pointers
 * alias the same array the converter overwrites not-yet-read raw MCU data
 * with already-converted pixels a few MCUs into the frame. Sized for the
 * worst realistic case (4:4:4, no chroma subsampling, 3 bytes/pixel) so
 * the whole frame lands in a single HAL_JPEG_DataReadyCallback() round
 * regardless of what subsampling the Pi's encoder picked -- if it only
 * needed one round for 4:2:0, staying single-round here removes any
 * dependency on jpeg_mcu_block_index tracking MCU counts vs. byte counts
 * correctly across multiple rounds. */
#define JPEG_MCU_BUF_SIZE   (AI_IMG_PIXELS * 3U)
static uint8_t jpeg_mcu_buf[JPEG_MCU_BUF_SIZE] __attribute__((aligned(32)));

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
  uint8_t  *in;

  if (!FRAME_SOURCE_GetLatestJPEG(ai_last_frame_id, &jpeg_data, &jpeg_len, &new_frame_id))
  {
    return 0;   /* nothing newer than what we already processed */
  }

  jpeg_decode_done  = 0;
  jpeg_decode_error = 0;
  pJpegColorConvertFunc = NULL;

  if (HAL_JPEG_Decode_IT(&hjpeg, jpeg_data, jpeg_len, jpeg_mcu_buf, sizeof(jpeg_mcu_buf)) != HAL_OK)
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

  /* If we timed out above, hjpeg is still mid-transaction (paused waiting
   * on input/output it's never going to get) and HAL_JPEG_STATE_BUSY_DECODING
   * never clears on its own -- every future HAL_JPEG_Decode_IT() call would
   * fail fast with HAL_BUSY forever after just one bad/truncated frame.
   * Abort resets hjpeg->State back to READY so the next frame gets a clean
   * shot regardless of what happened to this one. */
  if (!jpeg_decode_done)
  {
    (void)HAL_JPEG_Abort(&hjpeg);
    jpeg_decode_error = 1;
  }

  FRAME_SOURCE_ReleaseJPEG(jpeg_data);
  ai_last_frame_id = new_frame_id;

  if (!jpeg_decode_done || jpeg_decode_error)
  {
    return 0;
  }

  /* RGB565 -> interleaved uint8, NHWC layout, in one pass. run-12
   * (best_int8_uint8io) was retrained/requantized with a different input
   * contract than the old model -- see stai_network.h:
   *   STAI_NETWORK_IN_1_FORMAT  = STAI_FORMAT_U8   (was STAI_FORMAT_S8)
   *   STAI_NETWORK_IN_1_FLAGS   = ...CHANNEL_LAST  (was ...CHANNEL_FIRST)
   *   STAI_NETWORK_IN_1_SCALES  = 1/255, OFFSETS = 0   (was zero_point -128)
   * Unsigned with zero_point 0 means a pixel byte already in 0..255 IS the
   * quantized value as-is -- no "- 128" needed. Channel-last means R,G,B
   * are written interleaved per pixel instead of to three separate planes.
   * If you ever swap in a model quantized differently, re-derive this from
   * that model's actual STAI_NETWORK_IN_1_* macros instead of assuming
   * this still applies. */
  in = (uint8_t *)stai_input[0];

  for (i = 0; i < AI_IMG_PIXELS; i++)
  {
    uint16_t px = ((uint16_t *)ai_rgb565_buf)[i];
    uint8_t r5 = (px >> 11) & 0x1F;
    uint8_t g6 = (px >> 5)  & 0x3F;
    uint8_t b5 =  px        & 0x1F;

    in[i * 3U + 0U] = (uint8_t)((r5 << 3) | (r5 >> 2));
    in[i * 3U + 1U] = (uint8_t)((g6 << 2) | (g6 >> 4));
    in[i * 3U + 2U] = (uint8_t)((b5 << 3) | (b5 >> 2));
  }

  return 1;
  /* USER CODE END acquire_and_process_data */
}

/* Reads the class-score output tensor (9 class scores per anchor, already
 * through the network's own sigmoid + its own dedicated int8 quantization),
 * picks the single most confident class across every anchor, and shows it
 * on the OLED if it clears VND_CONF_THRESHOLD. We only care about "what
 * bill is this" here, not "where in frame" -- so this deliberately ignores
 * the box-coordinate output tensor entirely and skips NMS. If you later
 * want to report more than one bill at a time, or draw a box, that's
 * stai_output[BOX_OUT_INDEX] (shape [1,2100,4]) -- untouched by this split,
 * still one shared scale, still fine for pixel-range box coordinates.
 *
 * This model exposes TWO output tensors instead of one -- see the
 * VND_CONF_THRESHOLD comment above for why. The class-score tensor's shape
 * per the ONNX graph is [1, 2100, 9], anchor-major / class-minor
 * (index = anchor * 9 + class), which is what the indexing below assumes.
 *
 * IMPORTANT: confirm CLS_OUT_INDEX below against your regenerated
 * stai_network.h -- X-CUBE-AI assigns STAI_NETWORK_OUT_1_* / OUT_2_* in
 * whatever order it decides to emit the two outputs, which may not match
 * the order they're listed in the ONNX file. Check STAI_NETWORK_OUT_1_SHAPE
 * vs OUT_2_SHAPE (or CHANNEL, if named that way) -- whichever one shows 9
 * is the class-score tensor and its index (0 or 1) is what CLS_OUT_INDEX
 * should be. Also double check the axis order the generator actually used:
 * if STAI_NETWORK_OUT_x_FLAGS/SHAPE indicates anchors are the fast-varying
 * dimension instead (i.e. it kept the original [class][anchor] layout
 * rather than the ONNX's [anchor][class] layout), flip the indexing to
 * (c * n_anchors + a) instead of (a * n_classes + c). */
#define CLS_OUT_INDEX   1U   /* TODO: confirm against generated stai_network.h */

int post_process()
{
  /* USER CODE BEGIN post_process */
  static const float   cls_scale[]  = STAI_NETWORK_OUT_2_SCALES;
  static const int32_t cls_offset[] = STAI_NETWORK_OUT_2_OFFSETS;
  const int8_t *cls = (const int8_t *)stai_output[CLS_OUT_INDEX];
  const uint32_t n_anchors = 2100U;
  const uint32_t n_classes = VND_NUM_CLASSES;   /* 9 */

  float   best_score = 0.0f;
  int32_t best_class = -1;
  uint32_t a, c;

  for (a = 0; a < n_anchors; a++)
  {
    for (c = 0; c < n_classes; c++)
    {
      float score = ((float)cls[a * n_classes + c] - (float)cls_offset[0]) * cls_scale[0];
      if (score > best_score)
      {
        best_score = score;
        best_class = (int32_t)c;
      }
    }
  }

  if (best_class >= 0 && best_score >= VND_CONF_THRESHOLD)
  {
    char line[24];
    snprintf(line, sizeof(line), "%s VND", vnd_class_names[best_class]);
    AiDisplay_ShowDetection("Detected:", line);
  }
  else
  {
    AiDisplay_ShowDetection("No bill detected", NULL);
  }

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
