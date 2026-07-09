/**
  ******************************************************************************
  * @file    lwipopts.h
  * @brief   lwIP configuration for the BlindFold project (NUCLEO-N657X0-Q)
  ******************************************************************************
  * Adapted from STMicroelectronics' stm32n6-classic-coremw-apps reference
  * (Projects/NUCLEO-N657X0-Q/Applications/LwIP/LwIP_UDP_Echo_Server).
  *
  * One deliberate difference from that reference: LWIP_RAM_HEAP_POINTER is
  * NOT defined here. The reference relocates lwIP's heap to a fixed address
  * inside an MPU "non-cacheable" region because that example enables the
  * D-Cache. This project currently does NOT enable I/D-Cache anywhere (see
  * main.c), so there is no cache-coherency concern for DMA buffers and the
  * heap can safely live wherever the linker puts it. If you later enable
  * D-Cache for performance, you MUST reintroduce a non-cacheable, MPU-backed
  * region for the ETH DMA descriptors, the lwIP RX pool AND this heap -- see
  * the reference project's MPU_Config() for the pattern.
  ******************************************************************************
  */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/**
 * SYS_LIGHTWEIGHT_PROT==1: if you want inter-task protection for certain
 * critical regions during buffer allocation, deallocation and memory
 * allocation and deallocation.
 */
#define SYS_LIGHTWEIGHT_PROT    0

/**
 * NO_SYS==1: Provides VERY minimal functionality. No RTOS task backs the
 * stack here -- it's driven by polling calls from the main loop
 * (ethernetif_input / sys_check_timeouts / the periodic handlers).
 */
#define NO_SYS                  1

/* ---------- Memory options ---------- */
#define MEM_ALIGNMENT           4

/* MEM_SIZE: the size of the heap memory. If the application will send
   a lot of data that needs to be copied, this should be set high. */
#define MEM_SIZE                (14*1024)

/* MEMP_NUM_TCP_PCB: the number of simultaneously active TCP connections. */
#define MEMP_NUM_TCP_PCB        10

/* MEMP_NUM_TCP_SEG: the number of simultaneously queued TCP segments. */
#define MEMP_NUM_TCP_SEG        TCP_SND_QUEUELEN

/* ---------- Pbuf options ---------- */
#define PBUF_POOL_BUFSIZE       1536

/* LWIP_SUPPORT_CUSTOM_PBUF == 1: pass MAC Rx buffers straight to the stack,
   no copy needed (see the zero-copy RX pool in ethernetif.c). */
#define LWIP_SUPPORT_CUSTOM_PBUF      1

/*
   ------------------------------------------------
   ---------- Network Interfaces options ----------
   ------------------------------------------------
*/
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             1

/* ---------- TCP options ---------- */
#define LWIP_TCP                1
#define TCP_TTL                 255

/* TCP_MSS = (Ethernet MTU - IP header size - TCP header size) */
#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (4*TCP_MSS)
#define TCP_WND                 (4*TCP_MSS)

/* ---------- ICMP options ---------- */
#define LWIP_ICMP                       1

/* ---------- DHCP options ---------- */
#define LWIP_DHCP               1

/* ---------- UDP options ---------- */
#define LWIP_UDP                1
#define UDP_TTL                 255

/* ---------- Statistics options ---------- */
#define LWIP_STATS 0

/*
   --------------------------------------
   ---------- Checksum options ----------
   --------------------------------------
*/
/*
The STM32N6 ETH MAC can compute/verify IP, UDP, TCP and ICMP checksums in
hardware. MX_ETH1_Init() (main.c) already configures TxConfig for hardware
checksum insertion, and HAL_ETH_Init() enables MAC RX checksum offload by
default -- so we tell lwIP not to duplicate that work in software.
*/
#define CHECKSUM_BY_HARDWARE

#ifdef CHECKSUM_BY_HARDWARE
#define CHECKSUM_GEN_IP                 0
#define CHECKSUM_GEN_UDP                0
#define CHECKSUM_GEN_TCP                0
#define CHECKSUM_CHECK_IP               0
#define CHECKSUM_CHECK_UDP              0
#define CHECKSUM_CHECK_TCP              0
/* Hardware TCP/UDP checksum insertion isn't supported when the packet is an
   IPv4 fragment, so ICMP is still generated/checked here. */
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_ICMP             0
#else
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_ICMP             1
#endif

/*
   ----------------------------------------------
   ---------- Sequential layer options ----------
   ----------------------------------------------
*/
#define LWIP_NETCONN                    0

/*
   ------------------------------------
   ---------- Socket options ----------
   ------------------------------------
*/
#define LWIP_SOCKET                     0

#endif /* __LWIPOPTS_H__ */
