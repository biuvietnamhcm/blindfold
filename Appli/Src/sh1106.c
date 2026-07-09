/**
  ******************************************************************************
  * @file    sh1106.c
  * @brief   Minimal I2C driver for SH1106-based 128x64 (1.3") OLED displays.
  ******************************************************************************
  */
#include "sh1106.h"
#include <string.h>
#include <stdlib.h>

static I2C_HandleTypeDef *sh1106_hi2c;
static uint16_t sh1106_addr;   /* 8-bit HAL address actually in use */
static uint8_t  sh1106_buffer[SH1106_WIDTH * SH1106_PAGES];
static uint16_t sh1106_cursor_x;
static uint16_t sh1106_cursor_y;

/* ---- low level I2C helpers --------------------------------------------- */

static HAL_StatusTypeDef SH1106_WriteCommand(uint8_t cmd)
{
    /* Control byte 0x00 == "the following byte(s) are commands" */
    return HAL_I2C_Mem_Write(sh1106_hi2c, sh1106_addr, 0x00,
                              I2C_MEMADD_SIZE_8BIT, &cmd, 1,
                              SH1106_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef SH1106_WriteData(uint8_t *data, uint16_t size)
{
    /* Control byte 0x40 == "the following byte(s) are display data" */
    return HAL_I2C_Mem_Write(sh1106_hi2c, sh1106_addr, 0x40,
                              I2C_MEMADD_SIZE_8BIT, data, size,
                              SH1106_I2C_TIMEOUT_MS);
}

/* ---- init --------------------------------------------------------------- */

SH1106_Status SH1106_Init(I2C_HandleTypeDef *hi2c)
{
    static const uint16_t candidate_addrs[2] = { (0x3C << 1), (0x3D << 1) };
    uint8_t found = 0;

    sh1106_hi2c = hi2c;
    sh1106_addr = SH1106_I2C_ADDR;

    /* Probe: try the configured address first, then fall back so a wired-up
     * board "just works" even if the address strapping differs. */
    for (uint8_t i = 0; i < 2; i++)
    {
        if (HAL_I2C_IsDeviceReady(sh1106_hi2c, candidate_addrs[i], 3,
                                   SH1106_I2C_TIMEOUT_MS) == HAL_OK)
        {
            sh1106_addr = candidate_addrs[i];
            found = 1;
            break;
        }
    }

    if (!found)
    {
        return SH1106_ERR_NOT_FOUND;
    }

    static const uint8_t init_cmds[] = {
        0xAE,        /* display off */
        0xD5, 0x80,  /* clock divide ratio / oscillator freq */
        0xA8, 0x3F,  /* multiplex ratio = 64 (0x3F = 63 -> 1/64 duty) */
        0xD3, 0x00,  /* display offset = 0 */
        0x40,        /* display start line = 0 */
        0xAD, 0x8B,  /* DC-DC (SH1106 internal charge pump) on */
        0xA1,        /* segment remap: column 127 -> SEG0 */
        0xC8,        /* COM output scan direction: remapped */
        0xDA, 0x12,  /* COM pins hardware configuration */
        0x81, 0x80,  /* contrast control */
        0xD9, 0x22,  /* pre-charge period */
        0xDB, 0x35,  /* VCOM deselect level */
        0xA4,        /* entire display ON follows RAM content */
        0xA6,        /* normal (not inverted) display */
    };

    for (uint16_t i = 0; i < sizeof(init_cmds); i++)
    {
        if (SH1106_WriteCommand(init_cmds[i]) != HAL_OK)
        {
            return SH1106_ERR_I2C;
        }
    }

    SH1106_Fill(SH1106_COLOR_BLACK);
    SH1106_UpdateScreen();

    if (SH1106_WriteCommand(0xAF) != HAL_OK) /* display on */
    {
        return SH1106_ERR_I2C;
    }

    sh1106_cursor_x = 0;
    sh1106_cursor_y = 0;

    return SH1106_OK;
}

/* ---- framebuffer ops ------------------------------------------------------*/

void SH1106_Fill(SH1106_COLOR color)
{
    memset(sh1106_buffer, (color == SH1106_COLOR_BLACK) ? 0x00 : 0xFF,
           sizeof(sh1106_buffer));
}

void SH1106_UpdateScreen(void)
{
    for (uint8_t page = 0; page < SH1106_PAGES; page++)
    {
        uint8_t col_lo = (SH1106_COLUMN_OFFSET) & 0x0F;
        uint8_t col_hi = 0x10 | ((SH1106_COLUMN_OFFSET >> 4) & 0x0F);

        SH1106_WriteCommand(0xB0 + page); /* set page address */
        SH1106_WriteCommand(col_lo);      /* set lower column address nibble */
        SH1106_WriteCommand(col_hi);      /* set higher column address nibble */

        SH1106_WriteData(&sh1106_buffer[page * SH1106_WIDTH], SH1106_WIDTH);
    }
}

void SH1106_DrawPixel(int16_t x, int16_t y, SH1106_COLOR color)
{
    if (x < 0 || x >= (int16_t)SH1106_WIDTH || y < 0 || y >= (int16_t)SH1106_HEIGHT)
    {
        return;
    }

    uint16_t idx = (uint16_t)((y / 8) * SH1106_WIDTH + x);

    if (color == SH1106_COLOR_WHITE)
    {
        sh1106_buffer[idx] |= (uint8_t)(1U << (y % 8));
    }
    else
    {
        sh1106_buffer[idx] &= (uint8_t)~(1U << (y % 8));
    }
}

void SH1106_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, SH1106_COLOR color)
{
    /* Bresenham */
    int16_t dx = (int16_t)abs(x1 - x0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (int16_t)(-abs(y1 - y0));
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (1)
    {
        SH1106_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void SH1106_DrawRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, SH1106_COLOR color)
{
    SH1106_DrawLine(x0, y0, x1, y0, color);
    SH1106_DrawLine(x1, y0, x1, y1, color);
    SH1106_DrawLine(x1, y1, x0, y1, color);
    SH1106_DrawLine(x0, y1, x0, y0, color);
}

void SH1106_FillRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, SH1106_COLOR color)
{
    if (x1 < x0) { int16_t t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int16_t t = y0; y0 = y1; y1 = t; }

    for (int16_t y = y0; y <= y1; y++)
    {
        for (int16_t x = x0; x <= x1; x++)
        {
            SH1106_DrawPixel(x, y, color);
        }
    }
}

/* ---- text ---------------------------------------------------------------*/

void SH1106_SetCursor(uint8_t x, uint8_t y)
{
    sh1106_cursor_x = x;
    sh1106_cursor_y = y;
}

void SH1106_WriteChar(char ch, SH1106_COLOR color)
{
    if ((uint8_t)ch < FONT_FIRST_CHAR || (uint8_t)ch > FONT_LAST_CHAR)
    {
        ch = ' ';
    }

    if (sh1106_cursor_x + FONT_WIDTH > SH1106_WIDTH)
    {
        sh1106_cursor_x = 0;
        sh1106_cursor_y += FONT_HEIGHT;
    }
    if (sh1106_cursor_y + FONT_HEIGHT > SH1106_HEIGHT)
    {
        sh1106_cursor_y = 0;
    }

    const uint8_t *glyph = Font8x8[(uint8_t)ch - FONT_FIRST_CHAR];

    for (uint8_t col = 0; col < FONT_WIDTH; col++)
    {
        uint8_t colbits = glyph[col];
        for (uint8_t row = 0; row < FONT_HEIGHT; row++)
        {
            SH1106_COLOR px = (colbits & (1U << row)) ?
                                color :
                                ((color == SH1106_COLOR_WHITE) ? SH1106_COLOR_BLACK : SH1106_COLOR_WHITE);
            /* Only plot "on" pixels as background so we don't fight
             * whatever was already drawn behind the text; comment out the
             * else-branch above and just draw set bits if you want
             * transparent-background text. */
            SH1106_DrawPixel(sh1106_cursor_x + col, sh1106_cursor_y + row, px);
        }
    }

    sh1106_cursor_x += FONT_WIDTH;
}

void SH1106_WriteString(const char *str, SH1106_COLOR color)
{
    while (*str)
    {
        SH1106_WriteChar(*str++, color);
    }
}

/* ---- misc ---------------------------------------------------------------*/

void SH1106_SetContrast(uint8_t value)
{
    SH1106_WriteCommand(0x81);
    SH1106_WriteCommand(value);
}

void SH1106_DisplayOn(void)
{
    SH1106_WriteCommand(0xAF);
}

void SH1106_DisplayOff(void)
{
    SH1106_WriteCommand(0xAE);
}
