/**
  ******************************************************************************
  * @file    sh1106.h
  * @brief   Minimal I2C driver for SH1106-based 128x64 (1.3") OLED displays.
  *          Written for NUCLEO-N657X0-Q / STM32N657, using the hi2c1 instance
  *          already configured on PC1 (SDA) / PH9 (SCL) in this project.
  ******************************************************************************
  *
  * Wiring (module -> board):
  *   VCC -> 3V3
  *   GND -> GND
  *   SCL -> PH9  (I2C1_SCL)
  *   SDA -> PC1  (I2C1_SDA)
  *
  * IMPORTANT: the I2C1 lines are configured as open-drain alternate function
  * in this project (see HAL_I2C_MspInit). That is correct for I2C, but the
  * STM32N6 GPIOs do NOT have strong enough internal pull-ups for reliable
  * I2C at typical speeds -- if your OLED breakout does not already have
  * pull-up resistors on SDA/SCL (most 4-pin OLED breakout boards do), add
  * external 4.7k pull-ups from SDA/SCL to 3V3.
  ******************************************************************************
  */
#ifndef SH1106_H
#define SH1106_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>
#include "fonts.h"

/* ---- User configuration ------------------------------------------------- */

/* 7-bit I2C address, pre-shifted for HAL (which wants the 8-bit form).
 * Most 1.3" SH1106 boards are 0x3C. If SH1106_Init() reports
 * SH1106_ERR_NOT_FOUND, the driver automatically retries at 0x3D, so you
 * normally don't need to touch this. */
#define SH1106_I2C_ADDR         (0x3C << 1)

#define SH1106_WIDTH            128U
#define SH1106_HEIGHT           64U
#define SH1106_PAGES            (SH1106_HEIGHT / 8U)

/* SH1106 has 132 columns of GDDRAM but only 128 are wired to the panel,
 * and the visible window is not always centered the same way on every
 * board. 2 is correct for the overwhelming majority of 1.3" modules;
 * if your display shows a couple of columns of noise on the left/right
 * edge, or text is off by a couple of pixels, try 0 or 4 here. */
#define SH1106_COLUMN_OFFSET    2U

#define SH1106_I2C_TIMEOUT_MS   100U

/* ---- Types ---------------------------------------------------------------*/

typedef enum {
    SH1106_COLOR_BLACK = 0x00,
    SH1106_COLOR_WHITE = 0x01
} SH1106_COLOR;

typedef enum {
    SH1106_OK = 0,
    SH1106_ERR_NOT_FOUND,   /* no ACK from either 0x3C or 0x3D */
    SH1106_ERR_I2C          /* ACK'd but a later transfer failed */
} SH1106_Status;

/* ---- API -------------------------------------------------------------- */

/* Must be called once before any other SH1106_* function. Probes the bus,
 * runs the SH1106 init sequence, clears the panel and turns the display on. */
SH1106_Status SH1106_Init(I2C_HandleTypeDef *hi2c);

/* Fill the local framebuffer (does not touch the panel until you call
 * SH1106_UpdateScreen()). */
void SH1106_Fill(SH1106_COLOR color);

/* Push the local framebuffer to the panel over I2C. Call this after any
 * batch of drawing calls. */
void SH1106_UpdateScreen(void);

void SH1106_DrawPixel(int16_t x, int16_t y, SH1106_COLOR color);
void SH1106_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, SH1106_COLOR color);
void SH1106_DrawRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, SH1106_COLOR color);
void SH1106_FillRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, SH1106_COLOR color);

/* Text cursor, in pixels, top-left origin. */
void SH1106_SetCursor(uint8_t x, uint8_t y);
void SH1106_WriteChar(char ch, SH1106_COLOR color);
void SH1106_WriteString(const char *str, SH1106_COLOR color);

void SH1106_SetContrast(uint8_t value);
void SH1106_DisplayOn(void);
void SH1106_DisplayOff(void);

#ifdef __cplusplus
}
#endif

#endif /* SH1106_H */
