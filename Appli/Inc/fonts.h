/**
  ******************************************************************************
  * @file    fonts.h
  * @brief   8x8 monospace bitmap font for SH1106 OLED text rendering.
  *          Covers printable ASCII 0x20 (' ') .. 0x7E ('~').
  *          Storage format: one array entry per character, 8 bytes per
  *          character = 8 columns. Each byte is one column, bit0 = top
  *          pixel of that column, bit7 = bottom pixel (matches the
  *          page/column layout used by SH1106 GDDRAM).
  *
  *          Glyph data is the public-domain "font8x8_basic" bitmap font
  *          (IBM/VGA ROM heritage), chosen for clean, unambiguous strokes
  *          at 8x8 on a 1-bit OLED. See fonts.c for provenance details.
  ******************************************************************************
  */
#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

#define FONT_WIDTH   8U
#define FONT_HEIGHT  8U
#define FONT_FIRST_CHAR 0x20U
#define FONT_LAST_CHAR  0x7EU

extern const uint8_t Font8x8[95][8];

#endif /* FONTS_H */
