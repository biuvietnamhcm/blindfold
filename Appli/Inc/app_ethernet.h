/**
  ******************************************************************************
  * @file    app_ethernet.h
  * @brief   Header for app_ethernet.c module
  ******************************************************************************
  * Adapted from STMicroelectronics' stm32n6-classic-coremw-apps reference
  * for the BlindFold project.
  ******************************************************************************
  */
#ifndef __APP_ETHERNET_H
#define __APP_ETHERNET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/netif.h"

/* DHCP process states */
#define DHCP_OFF                   (uint8_t) 0
#define DHCP_START                 (uint8_t) 1
#define DHCP_WAIT_ADDRESS          (uint8_t) 2
#define DHCP_ADDRESS_ASSIGNED      (uint8_t) 3
#define DHCP_TIMEOUT                (uint8_t) 4
#define DHCP_LINK_DOWN             (uint8_t) 5

void ethernet_link_status_updated(struct netif *netif);
void Ethernet_Link_Periodic_Handle(struct netif *netif);
#if LWIP_DHCP
void DHCP_Process(struct netif *netif);
void DHCP_Periodic_Handle(struct netif *netif);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APP_ETHERNET_H */
