/**
  ******************************************************************************
  * @file    ethernetif.h
  * @brief   Header for ethernetif.c module
  ******************************************************************************
  * Adapted from STMicroelectronics' stm32n6-classic-coremw-apps reference
  * (Projects/NUCLEO-N657X0-Q/Applications/LwIP/LwIP_UDP_Echo_Server) for the
  * BlindFold project.
  ******************************************************************************
  */
#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "lwip/err.h"
#include "lwip/netif.h"

/* Exported types ------------------------------------------------------------*/
err_t ethernetif_init(struct netif *netif);
void ethernetif_input(struct netif *netif);
void ethernet_link_check_state(struct netif *netif);

/* TEMP DEBUG: dumps the raw LAN8742 PHY address + BSR register + decoded
 * link-state code to the OLED debug row. Added to chase a bug where the
 * board reports "link up" with no cable plugged in even though the RJ45
 * jack's own LEDs correctly go dark. Call periodically from main()'s loop.
 * Remove once the root cause is found. */
void ethernet_phy_debug_print(void);

#endif /* __ETHERNETIF_H__ */
