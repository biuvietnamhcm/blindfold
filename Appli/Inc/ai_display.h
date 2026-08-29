/**
  ******************************************************************************
  * @file    ai_display.h
  * @brief   Renders the latest banknote-classifier result onto the SH1106
  *          OLED. Owns screen rows y=16, y=24 (see main.c for the full
  *          layout: y=0 is the static title, y=48 is the frame counter).
  *
  *          Replaces the old net_display.h LAN/DHCP status section -- this
  *          project doesn't need live network status shown on-device, and
  *          "what banknote did the model just see" is the far more useful
  *          thing to put in front of the user here.
  ******************************************************************************
  */
#ifndef AI_DISPLAY_H
#define AI_DISPLAY_H

/* Redraws the 2 detection-result rows. Either argument may be NULL/empty
 * to leave that row blank. Each string is clipped to what fits on screen
 * (16 chars per row at the current font). Pushes the update to the panel
 * immediately (calls SH1106_UpdateScreen()). */
void AiDisplay_ShowDetection(const char *line1, const char *line2);

#endif /* AI_DISPLAY_H */
