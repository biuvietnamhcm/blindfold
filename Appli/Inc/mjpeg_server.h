/**
  ******************************************************************************
  * @file    mjpeg_server.h
  * @brief   Minimal MJPEG-over-HTTP server on lwIP's raw (callback) API --
  *          this project runs lwIP with NO_SYS=1 and LWIP_SOCKET=0, so the
  *          sockets/netconn APIs aren't available; this only uses tcp.h.
  *
  * Point a browser (or VLC's "Open Network Stream", or ffplay) at
  * http://<board-ip>/ and it'll start streaming -- the request path isn't
  * parsed at all, every connection just gets the multipart MJPEG stream.
  ******************************************************************************
  */
#ifndef MJPEG_SERVER_H
#define MJPEG_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* err_t is int8_t under the hood; keeping the return type generic here so
 * this header doesn't have to pull in lwIP's tcp.h. */
int8_t MJPEG_SERVER_Init(uint16_t port);

/* Call every iteration of the main loop, same as CAMERA_STREAM_Process().
 * This is what notices a new camera frame is ready and pushes it to any
 * connection that's currently idle waiting for one -- lwIP's own tcp_sent
 * callback handles resuming a send that stalled on a full TCP window, but
 * nothing else pokes an idle connection when fresh camera data shows up. */
void MJPEG_SERVER_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MJPEG_SERVER_H */
