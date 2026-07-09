/**
  ******************************************************************************
  * @file    net_display.h
  * @brief   Renders live Ethernet/DHCP status onto the SH1106 OLED.
  *          Owns screen rows y=16, y=24, y=32 (see main.c for the full
  *          layout: y=0 is the static title, y=48 is the uptime counter).
  ******************************************************************************
  */
#ifndef NET_DISPLAY_H
#define NET_DISPLAY_H

/* Redraws the 3 network status rows. Any argument may be NULL/empty to
 * leave that row blank. Each string is clipped to what fits on screen
 * (16 chars per row at the current font). Pushes the update to the panel
 * immediately (calls SH1106_UpdateScreen()). */
void NetDisplay_ShowStatus(const char *line1, const char *line2, const char *line3);

#endif /* NET_DISPLAY_H */
