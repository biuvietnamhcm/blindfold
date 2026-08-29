/**
  ******************************************************************************
  * @file    ai_display.c
  * @brief   Renders the latest banknote-classifier result onto the SH1106
  *          OLED.
  ******************************************************************************
  */
#include "ai_display.h"
#include "sh1106.h"

#define AI_ROW_Y0   16U
#define AI_ROW_Y1   24U

static void draw_row(uint8_t y, const char *text)
{
    /* Clear the full row width first so shorter new text doesn't leave
     * leftover pixels from a longer previous string. */
    SH1106_FillRectangle(0, y, SH1106_WIDTH - 1, y + FONT_HEIGHT - 1, SH1106_COLOR_BLACK);

    if (text != NULL)
    {
        SH1106_SetCursor(0, y);
        SH1106_WriteString(text, SH1106_COLOR_WHITE);
    }
}

void AiDisplay_ShowDetection(const char *line1, const char *line2)
{
    draw_row(AI_ROW_Y0, line1);
    draw_row(AI_ROW_Y1, line2);
    SH1106_UpdateScreen();
}
