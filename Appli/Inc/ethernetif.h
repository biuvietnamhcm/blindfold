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

#endif /* __ETHERNETIF_H__ */
