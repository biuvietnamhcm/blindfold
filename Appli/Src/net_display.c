/**
  ******************************************************************************
  * @file    net_display.c
  * @brief   Renders live Ethernet/DHCP status onto the SH1106 OLED.
  ******************************************************************************
  */
#include "net_display.h"
#include "sh1106.h"

#define NET_ROW_Y0   16U
#define NET_ROW_Y1   24U
#define NET_ROW_Y2   32U

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

void NetDisplay_ShowStatus(const char *line1, const char *line2, const char *line3)
{
    draw_row(NET_ROW_Y0, line1);
    draw_row(NET_ROW_Y1, line2);
    draw_row(NET_ROW_Y2, line3);
    SH1106_UpdateScreen();
}
