/**
 ******************************************************************************
 * @file    dhcpserver.c
 * @brief   Minimal, single-lease DHCP server for point-to-point Ethernet links.
 *
 * This board boots with LWIP_DHCP disabled (see lwipopts.h) and a fixed
 * static address instead -- there was never a DHCP server anywhere on this
 * link, so whatever host you plug into CN11 had to be given a matching
 * static IP by hand.
 *
 * This module flips that around. Because the RJ45 jack only ever has one
 * cable in it, there is only ever one DHCP client asking, so instead of a
 * real address pool / lease table this always offers the exact same fixed
 * address to whoever asks, for as long as the link is up. That's enough for
 * a plain "Automatic (DHCP)" profile on the far end to just work.
 *
 * Deliberately NOT a general-purpose DHCP server: only DISCOVER and REQUEST
 * are handled, there is no lease database, no multi-client support, no
 * option-52 (sname/file overload) support, and DECLINE/RELEASE/INFORM are
 * ignored. That matches the one job this link actually has.
 ******************************************************************************
 */
#include <string.h>
#include "dhcpserver.h"
#include "lwip/udp.h"
#include "lwip/prot/dhcp.h"

#define DHCPS_SERVER_PORT   67
#define DHCPS_CLIENT_PORT   68
#define DHCPS_LEASE_TIME_S  86400UL /* 24h -- irrelevant in practice, since
                                       there's no lease table to expire from;
                                       whoever asks always gets the same
                                       address again anyway. */

static struct udp_pcb *dhcps_pcb;
static struct netif   *dhcps_netif;
static ip4_addr_t      dhcps_offer;

/* Appends one DHCP option (tag, len, value...) into buf at *pos. */
static void dhcps_put_opt(u8_t *buf, u16_t *pos, u8_t tag, u8_t len, const void *val)
{
  buf[(*pos)++] = tag;
  buf[(*pos)++] = len;
  if (len != 0U) {
    memcpy(&buf[*pos], val, len);
    *pos = (u16_t)(*pos + len);
  }
}

/* Builds and broadcasts a DHCPOFFER / DHCPACK / DHCPNAK. req_xid/req_flags/
 * req_chaddr are copied out of the request before it's freed, since the
 * pbuf doesn't outlive dhcps_recv(). */
static void dhcps_send_reply(u8_t msg_type, u32_t req_xid, u16_t req_flags,
                              const u8_t *req_chaddr)
{
  struct pbuf *p;
  struct dhcp_msg *rep;
  u16_t opt_pos = 0;
  u8_t val;
  u32_t lease_be;
  ip4_addr_t server_id;

  p = pbuf_alloc(PBUF_TRANSPORT, DHCP_OPTIONS_OFS + 32U, PBUF_RAM);
  if (p == NULL) {
    return; /* out of memory -- client will just retry */
  }

  rep = (struct dhcp_msg *)p->payload;
  memset(rep, 0, DHCP_OPTIONS_OFS);

  rep->op    = DHCP_BOOTREPLY;
  rep->htype = 1;   /* Ethernet */
  rep->hlen  = 6;
  rep->xid   = req_xid;      /* already in on-wire byte order, echoed as-is */
  rep->flags = req_flags;    /* preserve the broadcast bit */
  memcpy(rep->chaddr, req_chaddr, DHCP_CHADDR_LEN);
  rep->cookie = PP_HTONL(DHCP_MAGIC_COOKIE);

  if (msg_type != DHCP_NAK) {
    ip4_addr_copy(rep->yiaddr, dhcps_offer);
  }

  val = msg_type;
  dhcps_put_opt(rep->options, &opt_pos, DHCP_OPTION_MESSAGE_TYPE, 1, &val);

  ip4_addr_copy(server_id, *netif_ip4_addr(dhcps_netif));
  dhcps_put_opt(rep->options, &opt_pos, DHCP_OPTION_SERVER_ID, 4, &server_id);

  if (msg_type == DHCP_OFFER || msg_type == DHCP_ACK) {
    ip4_addr_t mask;
    ip4_addr_copy(mask, *netif_ip4_netmask(dhcps_netif));
    dhcps_put_opt(rep->options, &opt_pos, DHCP_OPTION_SUBNET_MASK, 4, &mask);

    lease_be = PP_HTONL(DHCPS_LEASE_TIME_S);
    dhcps_put_opt(rep->options, &opt_pos, DHCP_OPTION_LEASE_TIME, 4, &lease_be);
  }

  rep->options[opt_pos++] = DHCP_OPTION_END;

  pbuf_realloc(p, (u16_t)(DHCP_OPTIONS_OFS + opt_pos));

  /* The client has no IP yet (or is in a state where broadcast is the safe
   * choice either way), so this always goes out as a link broadcast rather
   * than trying to unicast it. */
  udp_sendto_if(dhcps_pcb, p, IP_ADDR_BROADCAST, DHCPS_CLIENT_PORT, dhcps_netif);

  pbuf_free(p);
}

static void dhcps_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port)
{
  struct dhcp_msg hdr;
  u8_t msg_type = 0;
  u16_t opt_off;
  ip4_addr_t requested_ip;
  u8_t have_requested_ip = 0;
  ip4_addr_t server_id;
  u8_t have_server_id = 0;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(pcb);
  LWIP_UNUSED_ARG(addr);
  LWIP_UNUSED_ARG(port);

  if (p == NULL) {
    return;
  }
  if (p->tot_len < DHCP_OPTIONS_OFS + 4U) {
    pbuf_free(p);
    return; /* too short to be a real DHCP packet */
  }

  /* Pull the fixed-size header (everything up to where options start) out
   * as one contiguous local copy -- op/htype/hlen/hops, xid, secs, flags,
   * ciaddr/yiaddr/siaddr/giaddr, chaddr, sname, file, cookie. */
  pbuf_copy_partial(p, &hdr, DHCP_OPTIONS_OFS, 0);
  if (hdr.op != DHCP_BOOTREQUEST || lwip_ntohl(hdr.cookie) != DHCP_MAGIC_COOKIE) {
    pbuf_free(p);
    return;
  }

  /* Walk the options looking for message type (53), requested IP (50) and
   * server identifier (54). Options are read out of the pbuf a few bytes
   * at a time via pbuf_copy_partial() rather than assumed contiguous in
   * RAM, since pbuf_copy_partial() is itself safe against running past the
   * end of a short/malformed packet -- it just stops copying, so a bad
   * length byte can't walk this off into unrelated memory. */
  opt_off = DHCP_OPTIONS_OFS;
  for (;;) {
    u8_t tag, len, valbuf[4];

    if (pbuf_copy_partial(p, &tag, 1, opt_off) != 1) {
      break;
    }
    if (tag == DHCP_OPTION_END) {
      break;
    }
    if (tag == DHCP_OPTION_PAD) {
      opt_off = (u16_t)(opt_off + 1);
      continue;
    }
    if (pbuf_copy_partial(p, &len, 1, (u16_t)(opt_off + 1)) != 1) {
      break;
    }

    if (tag == DHCP_OPTION_MESSAGE_TYPE && len == 1) {
      pbuf_copy_partial(p, &msg_type, 1, (u16_t)(opt_off + 2));
    } else if (tag == DHCP_OPTION_REQUESTED_IP && len == 4) {
      pbuf_copy_partial(p, valbuf, 4, (u16_t)(opt_off + 2));
      memcpy(&requested_ip, valbuf, 4);
      have_requested_ip = 1;
    } else if (tag == DHCP_OPTION_SERVER_ID && len == 4) {
      pbuf_copy_partial(p, valbuf, 4, (u16_t)(opt_off + 2));
      memcpy(&server_id, valbuf, 4);
      have_server_id = 1;
    }

    opt_off = (u16_t)(opt_off + 2U + len);
    if (opt_off >= p->tot_len) {
      break;
    }
  }

  pbuf_free(p);

  if (msg_type == DHCP_DISCOVER) {
    dhcps_send_reply(DHCP_OFFER, hdr.xid, hdr.flags, hdr.chaddr);
  } else if (msg_type == DHCP_REQUEST) {
    ip4_addr_t candidate;

    /* Addressed to a different DHCP server (option 54 present and not
     * ours)? Stay quiet rather than NAK -- RFC 2131 4.3.2. Harmless on a
     * genuine point-to-point link, but correct if this ever ends up on a
     * segment with a real DHCP server too. */
    if (have_server_id && !ip4_addr_eq(&server_id, netif_ip4_addr(dhcps_netif))) {
      return;
    }

    if (have_requested_ip) {
      ip4_addr_copy(candidate, requested_ip);
    } else {
      ip4_addr_copy(candidate, hdr.ciaddr);
    }

    if (ip4_addr_eq(&candidate, &dhcps_offer)) {
      dhcps_send_reply(DHCP_ACK, hdr.xid, hdr.flags, hdr.chaddr);
    } else {
      dhcps_send_reply(DHCP_NAK, hdr.xid, hdr.flags, hdr.chaddr);
    }
  }
  /* DECLINE / RELEASE / INFORM: intentionally ignored -- see file header. */
}

void DHCPServer_Init(struct netif *netif, const ip4_addr_t *offered_ip)
{
  dhcps_netif = netif;
  ip4_addr_copy(dhcps_offer, *offered_ip);

  dhcps_pcb = udp_new();
  if (dhcps_pcb == NULL) {
    return;
  }
  udp_bind(dhcps_pcb, IP_ADDR_ANY, DHCPS_SERVER_PORT);
  udp_recv(dhcps_pcb, dhcps_recv, NULL);
}
