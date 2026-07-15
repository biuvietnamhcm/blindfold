/**
 ******************************************************************************
 * @file    dhcpserver.h
 * @brief   Minimal, single-lease DHCP server for point-to-point Ethernet links.
 ******************************************************************************
 */
#ifndef __DHCPSERVER_H
#define __DHCPSERVER_H

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Starts the DHCP server on the given netif.
 * @param  netif       The netif to bind to (its own address is used as the
 *                      DHCP server identifier and to derive the subnet mask
 *                      handed out to the client).
 * @param  offered_ip  The single fixed address this server will always
 *                      offer/ack. Must be in the same subnet as netif's own
 *                      address and must not equal it.
 * @note   Call once, after the netif is up and has its static address set.
 *         Must be called only once per boot (no corresponding de-init, since
 *         this project never tears ETH back down at runtime).
 */
void DHCPServer_Init(struct netif *netif, const ip4_addr_t *offered_ip);

#ifdef __cplusplus
}
#endif

#endif /* __DHCPSERVER_H */
