/* SPDX-License-Identifier: Apache-2.0 */

/* The server of the protocol that connects this board to the edge MCU.
 *
 * Note: the UART of the edge MCU also carries the serial console. Thus only a
 * build without a console starts this task. Refer to docs/ipc-protocol.md.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/kthread.h>

#include <telegraph/ipc.h>

#include "hazk03.h"
#include "version.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define IPC_DEVPATH       "/dev/ttyS0"

#define IPC_STACKSIZE     2048

/* This priority is above the scan loop of the display. The task waits on the
 * UART almost all of the time. Thus it takes little CPU time from the scan.
 */

#define IPC_PRIORITY      100

/* The receive line is idle after this period. A frame at 115200 baud takes
 * near 90 ms for the maximum payload. This value is for the short frames of
 * the control traffic.
 */

#define IPC_IDLE_MS       20

#define IPC_READ_CHUNK    128


/* The main panel holds 11 characters, and the sub panel holds 3 characters.
 * A longer text loses its end.
 */

#define IPC_TEXT_MAX      64

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ipc_ctx_s
{
  int                 fd;
  struct ipc_parser_s parser;
  uint8_t             tx[IPC_FRAME_MAX];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ipc_ctx_s *g_ipc;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void ipc_send(struct ipc_ctx_s *ctx, int len)
{
  const uint8_t *p = ctx->tx;

  if (len <= 0)
    {
      return;
    }

  while (len > 0)
    {
      ssize_t n = write(ctx->fd, p, (size_t)len);

      if (n <= 0)
        {
          return;
        }

      p   += n;
      len -= (int)n;
    }
}

/* Give the number of the frames that the receive buffer still accepts. */

static uint8_t ipc_credits(struct ipc_ctx_s *ctx)
{
  int pending = 0;
  int free_bytes;

  if (ioctl(ctx->fd, FIONREAD, (unsigned long)&pending) < 0)
    {
      pending = 0;
    }

  free_bytes = CONFIG_USART1_RXBUFSIZE - pending;

  if (free_bytes < 0)
    {
      free_bytes = 0;
    }

  free_bytes /= (int)IPC_CREDIT_UNIT;

  return (free_bytes > 255) ? 255 : (uint8_t)free_bytes;
}

static void ipc_ack(struct ipc_ctx_s *ctx, uint16_t corr_id)
{
  ipc_send(ctx, ipc_encode_ack(ctx->tx, sizeof(ctx->tx), corr_id,
                               ipc_credits(ctx)));
}

static void ipc_nack(struct ipc_ctx_s *ctx, uint16_t corr_id, uint8_t error)
{
  ipc_send(ctx, ipc_encode_nack(ctx->tx, sizeof(ctx->tx), corr_id, error));
}

/* Transmit a log line. The frame is a push, thus it carries no request. */

static void ipc_log(struct ipc_ctx_s *ctx, const char *text)
{
  size_t len = strlen(text);

  if (len > IPC_TEXT_MAX)
    {
      len = IPC_TEXT_MAX;
    }

  ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), IPC_OP_LOG,
                           IPC_CORR_ID_PUSH, text, (uint16_t)len));
}

static void ipc_send_state(struct ipc_ctx_s *ctx, uint16_t corr_id)
{
  uint8_t payload[IPC_STATE_LEN + IPC_FWVER_MAX];
  const struct ipc_stats_s *st = &ctx->parser.stats;
  size_t vlen = strlen(HAZK03_VERSION);

  ipc_put_u32(&payload[IPC_STATE_TIME], (uint32_t)time(NULL));
  ipc_put_u16(&payload[IPC_STATE_TEMP],
              (uint16_t)hazk03_display_temperature());
  ipc_put_u16(&payload[IPC_STATE_FRAMES], (uint16_t)st->frames);
  ipc_put_u16(&payload[IPC_STATE_CRC_ERR], (uint16_t)st->crc_errors);

  payload[IPC_STATE_RESYNC]  = (uint8_t)st->resyncs;
  payload[IPC_STATE_VERSION] = IPC_PROTO_VERSION;

  if (vlen > IPC_FWVER_MAX)
    {
      vlen = IPC_FWVER_MAX;
    }

  memcpy(&payload[IPC_STATE_FWVER], HAZK03_VERSION, vlen);

  ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), IPC_OP_STATE, corr_id,
                           payload, (uint16_t)(IPC_STATE_LEN + vlen)));
}

static void ipc_set_time(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame)
{
  struct timespec ts;

  if (frame->payload_len != IPC_SET_TIME_LEN &&
      frame->payload_len != IPC_SET_TIME_TZ_LEN)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  ts.tv_sec  = (time_t)ipc_get_u32(&frame->payload[IPC_SET_TIME_UTC]);
  ts.tv_nsec = 0;

  /* The panels show the local time. The RTC keeps UTC, thus the offset stays
   * with the display.
   */

  if (frame->payload_len == IPC_SET_TIME_TZ_LEN)
    {
      hazk03_display_utcoffset(
          (int16_t)ipc_get_u16(&frame->payload[IPC_SET_TIME_OFFSET]));
    }

  /* The system clock is the DS3231, and a battery holds that device. Thus
   * this value stays correct after a power interruption.
   */

  if (clock_settime(CLOCK_REALTIME, &ts) < 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
      return;
    }

  ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_text(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame, int panel)
{
  char text[IPC_TEXT_MAX];
  size_t len = frame->payload_len;

  if (len > sizeof(text))
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  /* The payload of a frame points into the buffer of the parser. Thus this
   * step copies the text before the next frame arrives.
   */

  memcpy(text, frame->payload, len);
  hazk03_display_text(panel, text, len);

  ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_bright(struct ipc_ctx_s *ctx,
                           const struct ipc_frame_s *frame)
{
  uint8_t digits;
  uint8_t panels;

  if (frame->payload_len != IPC_SET_BRIGHT_LEN &&
      frame->payload_len != IPC_SET_BRIGHT2_LEN)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  digits = frame->payload[0];
  panels = (frame->payload_len == IPC_SET_BRIGHT2_LEN) ?
           frame->payload[1] : digits;

  if (digits > IPC_BRIGHT_MAX || panels > IPC_BRIGHT_MAX)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  hazk03_display_brightness(digits, panels);
  ipc_ack(ctx, frame->corr_id);
}

static void ipc_on_frame(void *arg, const struct ipc_frame_s *frame)
{
  struct ipc_ctx_s *ctx = (struct ipc_ctx_s *)arg;

  switch (frame->opcode)
    {
      case IPC_OP_SET_TIME:
        ipc_set_time(ctx, frame);
        break;

      case IPC_OP_SET_LARGE:
        ipc_set_text(ctx, frame, HAZK03_PANEL_MAIN);
        break;

      case IPC_OP_SET_SMALL:
        ipc_set_text(ctx, frame, HAZK03_PANEL_SUB);
        break;

      case IPC_OP_SET_BRIGHT:
        ipc_set_bright(ctx, frame);
        break;

      case IPC_OP_GET_STATE:
        ipc_send_state(ctx, frame->corr_id);
        break;

      /* A flash operation uses the system bootloader. The edge MCU holds
       * BOOT0 and it applies a reset. Thus this firmware needs no support.
       */

      case IPC_OP_FLASH:
        ipc_nack(ctx, frame->corr_id, IPC_ERR_UNSUPPORTED);
        break;

      default:
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_OPCODE);
        break;
    }
}

static int ipc_server(int argc, char *argv[])
{
  struct ipc_ctx_s *ctx = g_ipc;
  uint8_t buf[IPC_READ_CHUNK];

  ipc_log(ctx, "telegraph ipc ready");

  for (; ; )
    {
      struct pollfd pfd;
      int ret;

      pfd.fd     = ctx->fd;
      pfd.events = POLLIN;

      /* A wait with a limit is necessary only for a partial frame. Without
       * that condition the task blocks. Thus it does not interrupt the scan
       * loop of the display 50 times each second.
       */

      ret = poll(&pfd, 1,
                 ipc_parser_pending(&ctx->parser) ? IPC_IDLE_MS : -1);

      if (ret > 0)
        {
          ssize_t n = read(ctx->fd, buf, sizeof(buf));

          if (n > 0)
            {
              ipc_parser_push(&ctx->parser, buf, (size_t)n,
                              ipc_on_frame, ctx);
            }
        }
      else if (ret == 0 && ipc_parser_pending(&ctx->parser))
        {
          /* The line is idle. A false start-of-frame byte holds the parser
           * here. This call discards that candidate.
           */

          ipc_parser_timeout(&ctx->parser, ipc_on_frame, ctx);
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hazk03_ipc_init(void)
{
  struct ipc_ctx_s *ctx;
  int ret;

  ctx = (struct ipc_ctx_s *)malloc(sizeof(struct ipc_ctx_s));
  if (ctx == NULL)
    {
      return -ENOMEM;
    }

  ctx->fd = open(IPC_DEVPATH, O_RDWR);
  if (ctx->fd < 0)
    {
      free(ctx);
      return -ENODEV;
    }

  ipc_parser_init(&ctx->parser);
  g_ipc = ctx;

  ret = kthread_create("ipc", IPC_PRIORITY, IPC_STACKSIZE, ipc_server, NULL);
  if (ret < 0)
    {
      close(ctx->fd);
      free(ctx);
      g_ipc = NULL;
      return ret;
    }

  return OK;
}
