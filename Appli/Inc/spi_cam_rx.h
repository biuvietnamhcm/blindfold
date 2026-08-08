/**
  ******************************************************************************
  * @file    spi_cam_rx.h
  * @brief   Receive side of the camera bridge: this is the "TODO (not done
  *          here on purpose)" from frame_source.h, finally done.
  *
  * Owns everything spi_cam_sender.py's docstring promises the STM32 side
  * will do: arm SPI5 (DMA, not interrupt-per-byte -- 8MHz is a lot of IRQs),
  * parse+validate the FRM1 header, CRC32 the payload, and drive the READY
  * handshake pin. frame_source.c stays exactly as hardware-agnostic as its
  * own header says it should -- this file is the only thing in the project
  * that knows SPI5, DMA, or the wire protocol exist, and the only thing it
  * hands frame_source.c is a plain (pointer, length) via
  * FRAME_SOURCE_PushFrame(), same as any other caller would.
  *
  * WIRING (see spi_cam_sender.py for the Pi side of all of this):
  *   Nucleo CN14 D13 / PE15  <-- SPI5_SCK   (already CubeMX-configured)
  *   Nucleo CN14 D11 / PG2   <-- SPI5_MOSI  (already CubeMX-configured)
  *   Nucleo CN14 D10 / PA3   <-- SPI5_NSS / Pi CE0 -- repurposed by this
  *                               module as a plain EXTI input; see the
  *                               "why PA3 is safe to steal" comment in
  *                               spi_cam_rx.c before assuming that's a bug.
  *   Nucleo CN14 D8  / PD12  --> READY, to the Pi's GPIO27 (BCM numbering,
  *                               physical pin 13). Picked because it's on
  *                               the same CN14 header as the SPI5 wiring
  *                               above, completely unused elsewhere on this
  *                               board (no RIF grant, no peripheral, no
  *                               ST-supplied alternate function tied to it
  *                               in this project), so it can't collide with
  *                               anything already on the board.
  *   MISO (D12 / PG1) is still deliberately left alone -- the Pi never
  *   wires it, and SPI5 doesn't need it in RXONLY direction.
  *
  * USAGE: call SPI_CAM_RX_Init() once at startup, any time after
  * MX_SPI5_Init() has run (it re-purposes one of that function's pins, so
  * it must come after, not before). Call SPI_CAM_RX_Process() every
  * iteration of the main loop, same spirit as FRAME_SOURCE_Process() /
  * MJPEG_SERVER_Poll() -- this is deliberately where FRAME_SOURCE_PushFrame()
  * actually gets called, kept off the ISR/DMA-complete path per the note in
  * frame_source.h about not sharing that state with mjpeg_server.c's reader
  * side from interrupt context.
  ******************************************************************************
  */
#ifndef SPI_CAM_RX_H
#define SPI_CAM_RX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SPI_CAM_RX_Init(void);
void SPI_CAM_RX_Process(void);

/* Lightweight diagnostics -- frames actually accepted are already visible
 * via FRAME_SOURCE_GetFrameCount() (main.c already puts that on the OLED);
 * these four cover the ways a frame can be lost on *this* end of the link,
 * the STM32-side mirror of spi_cam_sender.py's own dropped_* counters. */
uint32_t SPI_CAM_RX_GetBadHeaderCount(void);     /* bad magic / bad length  */
uint32_t SPI_CAM_RX_GetBadCrcCount(void);        /* header ok, CRC32 wrong  */
uint32_t SPI_CAM_RX_GetSpiErrorCount(void);      /* OVR/MODF/FRE/UDR faults */
uint32_t SPI_CAM_RX_GetStuckRecoveryCount(void); /* mid-payload stall, self-healed */

#ifdef __cplusplus
}
#endif

#endif /* SPI_CAM_RX_H */
