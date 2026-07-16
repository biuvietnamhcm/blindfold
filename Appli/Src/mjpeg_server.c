/**
  ******************************************************************************
  * @file    mjpeg_server.c
  * @brief   See mjpeg_server.h.
  *
  * State machine per connection:
  *
  *   WAIT_REQUEST -> SEND_HTTP_HEADER -> WAIT_FRAME -+-> SEND_BOUNDARY
  *                                            ^       |      |
  *                                            |       v      v
  *                                            +-- SEND_TRAILER <- SEND_FRAME
  *
  * WAIT_FRAME is where a connection sits between frames; MJPEG_SERVER_Poll()
  * (called from the main loop) is what notices a new frame and kicks it
  * into SEND_BOUNDARY. Every SEND_* state can also be re-entered from the
  * lwIP `sent` callback when a write stalled because the TCP window was
  * full -- mjpeg_conn_pump() is idempotent/resumable for exactly that
  * reason, it always picks up from *_sent byte offsets rather than
  * assuming it's starting fresh.
  ******************************************************************************
  */
#include "mjpeg_server.h"
#include "camera_stream.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>

#define MJPEG_MAX_CONNECTIONS   2U
#define MJPEG_MSG_BUF_SIZE      160U

typedef enum
{
  MJPEG_FREE = 0,
  MJPEG_WAIT_REQUEST,
  MJPEG_SEND_HTTP_HEADER,
  MJPEG_WAIT_FRAME,
  MJPEG_SEND_BOUNDARY,
  MJPEG_SEND_FRAME,
  MJPEG_SEND_TRAILER,
  MJPEG_CLOSING
} mjpeg_state_t;

typedef struct
{
  uint8_t          in_use;
  struct tcp_pcb  *pcb;
  mjpeg_state_t    state;

  char             msg[MJPEG_MSG_BUF_SIZE];
  uint16_t         msg_len;
  uint16_t         msg_sent;

  uint8_t         *frame_data;   /* locked via CAMERA_STREAM_GetLatestJPEG */
  uint32_t         frame_len;
  uint32_t         frame_sent;
  uint32_t         last_frame_id;
} mjpeg_conn_t;

static mjpeg_conn_t s_conns[MJPEG_MAX_CONNECTIONS];
static struct tcp_pcb *s_listen_pcb;

static const char HTTP_HEADER[] =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
  "\r\n";

static void mjpeg_conn_release(mjpeg_conn_t *c)
{
  if (c->frame_data != NULL)
  {
    CAMERA_STREAM_ReleaseJPEG(c->frame_data);
    c->frame_data = NULL;
  }
  c->in_use = 0;
  c->pcb = NULL;
  c->state = MJPEG_FREE;
}

static void mjpeg_conn_abort(mjpeg_conn_t *c)
{
  if (c->pcb != NULL)
  {
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_sent(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_poll(c->pcb, NULL, 0);
    tcp_close(c->pcb);
  }
  mjpeg_conn_release(c);
}

/* Drives whatever c->state currently is as far as the TCP send window
 * allows. Safe to call repeatedly (from recv/sent/poll/main-loop) --
 * always resumes from *_sent, never restarts a part that's in flight. */
static void mjpeg_conn_pump(mjpeg_conn_t *c)
{
  uint8_t keep_going = 1;

  while (keep_going)
  {
    keep_going = 0;

    switch (c->state)
    {
      case MJPEG_SEND_HTTP_HEADER:
      {
        uint16_t remain = c->msg_len - c->msg_sent;
        uint16_t avail = tcp_sndbuf(c->pcb);
        uint16_t n;

        if (remain == 0U) { c->state = MJPEG_WAIT_FRAME; keep_going = 1; break; }
        if (avail == 0U) { return; }
        n = (remain < avail) ? remain : avail;
        if (tcp_write(c->pcb, &c->msg[c->msg_sent], n, TCP_WRITE_FLAG_COPY) != ERR_OK) { return; }
        c->msg_sent += n;
        tcp_output(c->pcb);
        if (c->msg_sent >= c->msg_len) { c->state = MJPEG_WAIT_FRAME; keep_going = 1; }
        break;
      }

      case MJPEG_WAIT_FRAME:
      {
        uint8_t *data;
        uint32_t len, fid;

        if (CAMERA_STREAM_GetLatestJPEG(c->last_frame_id, &data, &len, &fid) == 0U)
        {
          c->last_frame_id = fid;
          return; /* nothing new yet -- MJPEG_SERVER_Poll() will retry */
        }

        c->frame_data = data;
        c->frame_len = len;
        c->frame_sent = 0;
        c->last_frame_id = fid;
        c->msg_len = (uint16_t)snprintf(c->msg, MJPEG_MSG_BUF_SIZE,
                                         "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %lu\r\n\r\n",
                                         (unsigned long)len);
        c->msg_sent = 0;
        c->state = MJPEG_SEND_BOUNDARY;
        keep_going = 1;
        break;
      }

      case MJPEG_SEND_BOUNDARY:
      {
        uint16_t remain = c->msg_len - c->msg_sent;
        uint16_t avail = tcp_sndbuf(c->pcb);
        uint16_t n;

        if (remain == 0U) { c->state = MJPEG_SEND_FRAME; keep_going = 1; break; }
        if (avail == 0U) { return; }
        n = (remain < avail) ? remain : avail;
        if (tcp_write(c->pcb, &c->msg[c->msg_sent], n, TCP_WRITE_FLAG_COPY) != ERR_OK) { return; }
        c->msg_sent += n;
        tcp_output(c->pcb);
        if (c->msg_sent >= c->msg_len) { c->state = MJPEG_SEND_FRAME; keep_going = 1; }
        break;
      }

      case MJPEG_SEND_FRAME:
      {
        uint32_t remain = c->frame_len - c->frame_sent;
        uint16_t avail = tcp_sndbuf(c->pcb);
        uint16_t n;

        if (remain == 0U)
        {
          CAMERA_STREAM_ReleaseJPEG(c->frame_data);
          c->frame_data = NULL;
          c->msg[0] = '\r'; c->msg[1] = '\n';
          c->msg_len = 2U;
          c->msg_sent = 0;
          c->state = MJPEG_SEND_TRAILER;
          keep_going = 1;
          break;
        }
        if (avail == 0U) { return; }
        n = (remain < (uint32_t)avail) ? (uint16_t)remain : avail;
        /* Copied (not zero-copy) on purpose: it lets us release the frame
         * lock the moment we've handed all of it to tcp_write(), instead
         * of having to track ACKs before it's safe to let the encoder
         * reuse this slot. TCP_SND_BUF is a few KB, well inside this
         * project's lwIP heap (see lwipopts.h MEM_SIZE), so the copy is
         * cheap enough here. */
        if (tcp_write(c->pcb, c->frame_data + c->frame_sent, n, TCP_WRITE_FLAG_COPY) != ERR_OK) { return; }
        c->frame_sent += n;
        tcp_output(c->pcb);
        if (c->frame_sent < c->frame_len) { return; }
        keep_going = 1;   /* loop straight back around to release + trailer */
        break;
      }

      case MJPEG_SEND_TRAILER:
      {
        uint16_t remain = c->msg_len - c->msg_sent;
        uint16_t avail = tcp_sndbuf(c->pcb);
        uint16_t n;

        if (remain == 0U) { c->state = MJPEG_WAIT_FRAME; keep_going = 1; break; }
        if (avail == 0U) { return; }
        n = (remain < avail) ? remain : avail;
        if (tcp_write(c->pcb, &c->msg[c->msg_sent], n, TCP_WRITE_FLAG_COPY) != ERR_OK) { return; }
        c->msg_sent += n;
        tcp_output(c->pcb);
        if (c->msg_sent >= c->msg_len) { c->state = MJPEG_WAIT_FRAME; keep_going = 1; }
        break;
      }

      default:
        return;
    }
  }
}

static err_t mjpeg_on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
  mjpeg_conn_t *c = (mjpeg_conn_t *)arg;
  (void)pcb; (void)len;
  if ((c != NULL) && c->in_use) { mjpeg_conn_pump(c); }
  return ERR_OK;
}

static err_t mjpeg_on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  mjpeg_conn_t *c = (mjpeg_conn_t *)arg;

  if (err != ERR_OK)
  {
    if (p != NULL) { pbuf_free(p); }
    if (c != NULL) { mjpeg_conn_release(c); }
    return ERR_OK;
  }

  if (p == NULL)
  {
    /* Remote closed. */
    if (c != NULL) { mjpeg_conn_abort(c); }
    return ERR_OK;
  }

  tcp_recved(pcb, p->tot_len);

  /* We don't care what the request says (single-purpose stream endpoint)
   * -- receiving anything at all is our cue that the GET line has arrived
   * and it's safe to answer. */
  if ((c != NULL) && (c->state == MJPEG_WAIT_REQUEST))
  {
    memcpy(c->msg, HTTP_HEADER, sizeof(HTTP_HEADER) - 1U);
    c->msg_len = (uint16_t)(sizeof(HTTP_HEADER) - 1U);
    c->msg_sent = 0;
    c->state = MJPEG_SEND_HTTP_HEADER;
    mjpeg_conn_pump(c);
  }

  pbuf_free(p);
  return ERR_OK;
}

static void mjpeg_on_err(void *arg, err_t err)
{
  mjpeg_conn_t *c = (mjpeg_conn_t *)arg;
  (void)err;
  /* The pcb is already invalid by the time this fires -- just release our
   * side (don't call tcp_close/tcp_abort on it). */
  if (c != NULL) { mjpeg_conn_release(c); }
}

static err_t mjpeg_on_poll(void *arg, struct tcp_pcb *pcb)
{
  mjpeg_conn_t *c = (mjpeg_conn_t *)arg;
  (void)pcb;
  if ((c != NULL) && c->in_use) { mjpeg_conn_pump(c); }
  return ERR_OK;
}

static err_t mjpeg_on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  mjpeg_conn_t *c = NULL;
  uint32_t i;
  (void)arg;

  if (err != ERR_OK) { return err; }

  for (i = 0; i < MJPEG_MAX_CONNECTIONS; i++)
  {
    if (!s_conns[i].in_use) { c = &s_conns[i]; break; }
  }
  if (c == NULL)
  {
    tcp_abort(newpcb);   /* no free slot -- shed this connection */
    return ERR_ABRT;
  }

  memset(c, 0, sizeof(*c));
  c->in_use = 1;
  c->pcb = newpcb;
  c->state = MJPEG_WAIT_REQUEST;

  tcp_arg(newpcb, c);
  tcp_recv(newpcb, mjpeg_on_recv);
  tcp_sent(newpcb, mjpeg_on_sent);
  tcp_err(newpcb, mjpeg_on_err);
  tcp_poll(newpcb, mjpeg_on_poll, 2U);   /* ~1s at the default TCP_SLOW_INTERVAL,
                                             just a safety net -- see
                                             MJPEG_SERVER_Poll() for the real
                                             "new frame is ready" trigger */
  tcp_nagle_disable(newpcb);             /* frames are already whole writes;
                                             Nagle just adds latency here */
  return ERR_OK;
}

int8_t MJPEG_SERVER_Init(uint16_t port)
{
  struct tcp_pcb *pcb;
  err_t err;

  memset(s_conns, 0, sizeof(s_conns));

  pcb = tcp_new();
  if (pcb == NULL) { return -1; }

  err = tcp_bind(pcb, IP_ADDR_ANY, port);
  if (err != ERR_OK)
  {
    tcp_close(pcb);
    return -1;
  }

  s_listen_pcb = tcp_listen_with_backlog(pcb, MJPEG_MAX_CONNECTIONS);
  if (s_listen_pcb == NULL)
  {
    tcp_close(pcb);
    return -1;
  }

  tcp_accept(s_listen_pcb, mjpeg_on_accept);
  return 0;
}

void MJPEG_SERVER_Poll(void)
{
  uint32_t i;

  for (i = 0; i < MJPEG_MAX_CONNECTIONS; i++)
  {
    if (s_conns[i].in_use && (s_conns[i].state == MJPEG_WAIT_FRAME))
    {
      mjpeg_conn_pump(&s_conns[i]);
    }
  }
}
