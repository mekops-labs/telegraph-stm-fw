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
#include <sys/stat.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/kthread.h>

#include <telegraph/ipc.h>

#include "fontext.h"
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

#ifdef CONFIG_FS_SMARTFS
/* The file of the assets that a transfer holds open. */

static int g_asset_fd = -1;
#endif

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

/* Take the panel from the first byte of a payload. */

static int ipc_panel_of(const struct ipc_frame_s *frame)
{
  if (frame->payload_len == 0 || frame->payload[0] > IPC_PANEL_SUB)
    {
      return -1;
    }

  return (frame->payload[0] == IPC_PANEL_SUB) ? HAZK03_PANEL_SUB
                                              : HAZK03_PANEL_MAIN;
}

static void ipc_set_text(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame)
{
  int panel = ipc_panel_of(frame);

  char text[IPC_TEXT_MAX];
  uint8_t align = IPC_ALIGN_CENTRE;
  uint8_t valign = IPC_VALIGN_MIDDLE;
  size_t len = 0;

  if (panel < 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  /* The byte after the panel carries the attributes. A payload of the panel
   * alone clears that panel, thus it needs no such byte.
   */

  if (frame->payload_len > IPC_TEXT_ATTRS)
    {
      uint8_t attrs = frame->payload[IPC_TEXT_ATTRS];

      if ((attrs & ~IPC_TEXT_ATTR_MASK) != 0)
        {
          ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
          return;
        }

      align  = attrs & IPC_ALIGN_MASK;
      valign = (attrs & IPC_VALIGN_MASK) >> IPC_VALIGN_SHIFT;

      if (align > IPC_ALIGN_RIGHT || valign > IPC_VALIGN_BOTTOM)
        {
          ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
          return;
        }

      len = frame->payload_len - IPC_TEXT_BODY;
    }

  if (len > sizeof(text))
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  /* The payload of a frame points into the buffer of the parser. Thus this
   * step copies the text before the next frame arrives.
   */

  memcpy(text, &frame->payload[IPC_TEXT_BODY], len);
  hazk03_display_text(panel, text, len, align, valign);

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

static void ipc_animate(struct ipc_ctx_s *ctx,
                        const struct ipc_frame_s *frame)
{
  int panel = ipc_panel_of(frame);
  uint8_t flags;
  bool text;
  bool file;
  int srcw;
  int srch;
  size_t srclen;
  int ret;

  if (panel < 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  if (frame->payload_len <= IPC_ANIM_BODY)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  flags = frame->payload[IPC_ANIM_FLAGS];

  if ((flags & ~IPC_ANIM_FLAG_MASK) != 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  text   = (flags & IPC_ANIM_TEXT) != 0;
  file   = (flags & IPC_ANIM_FILE) != 0;
  srcw   = frame->payload[IPC_ANIM_SRCW];
  srch   = frame->payload[IPC_ANIM_SRCH];
  srclen = frame->payload_len - IPC_ANIM_BODY;

  if (!text && !file)
    {
      /* The pixels must match the size of the source. */

      if (srcw == 0 || srch == 0 ||
          srclen != (size_t)(((srcw + 7) / 8) * srch))
        {
          ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
          return;
        }
    }

  ret = hazk03_display_animate(panel,
                               frame->payload[IPC_ANIM_X],
                               frame->payload[IPC_ANIM_Y],
                               frame->payload[IPC_ANIM_W],
                               frame->payload[IPC_ANIM_H],
                               (flags & IPC_ANIM_VERTICAL) != 0,
                               ipc_get_u16(&frame->payload[IPC_ANIM_PERIOD]),
                               frame->payload[IPC_ANIM_STEP],
                               text, file, srcw, srch,
                               &frame->payload[IPC_ANIM_BODY], srclen);

  if (ret < 0)
    {
      ipc_nack(ctx, frame->corr_id,
               (ret == -E2BIG) ? IPC_ERR_BAD_LENGTH : IPC_ERR_BAD_PAYLOAD);
      return;
    }

  ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_pixels(struct ipc_ctx_s *ctx,
                           const struct ipc_frame_s *frame)
{
  int panel = ipc_panel_of(frame);
  uint8_t x;
  uint8_t y;
  uint8_t w;
  uint8_t h;
  uint16_t need;

  if (panel < 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  if (frame->payload_len < IPC_PIX_HEADER)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  x = frame->payload[IPC_PIX_X];
  y = frame->payload[IPC_PIX_Y];
  w = frame->payload[IPC_PIX_W];
  h = frame->payload[IPC_PIX_H];

  if (w == 0 || h == 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  need = (uint16_t)IPC_PIX_HEADER + (uint16_t)(((w + 7) / 8) * h);

  if (frame->payload_len != need)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  hazk03_display_pixels(panel, x, y, w, h, &frame->payload[IPC_PIX_BITS]);
  ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_tempoff(struct ipc_ctx_s *ctx,
                            const struct ipc_frame_s *frame)
{
  if (frame->payload_len != IPC_SET_TEMPOFF_LEN)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  hazk03_display_tempoffset((int16_t)ipc_get_u16(frame->payload));
  ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_sleep(struct ipc_ctx_s *ctx,
                          const struct ipc_frame_s *frame)
{
  uint16_t start;
  uint16_t end;

  if (frame->payload_len != IPC_SET_SLEEP_LEN)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  start = ipc_get_u16(&frame->payload[IPC_SLEEP_START]);
  end   = ipc_get_u16(&frame->payload[IPC_SLEEP_END]);

  /* A minute above the day is invalid. The value that stops the function is
   * the one exception.
   */

  if ((start >= IPC_MINUTES_PER_DAY && start != IPC_SLEEP_OFF) ||
      (end >= IPC_MINUTES_PER_DAY && end != IPC_SLEEP_OFF))
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  hazk03_display_sleep(start, end);
  ipc_ack(ctx, frame->corr_id);
}

#ifdef CONFIG_FS_SMARTFS
/* Make the directory of a path. A path with no directory does nothing. */

static void ipc_asset_mkdir(char *path)
{
  char *slash = strrchr(path, '/');

  if (slash == NULL || slash == path)
    {
      return;
    }

  *slash = '\0';
  mkdir(path, 0777);
  *slash = '/';
}

static void ipc_write_asset(struct ipc_ctx_s *ctx,
                            const struct ipc_frame_s *frame)
{
  char path[IPC_ASSET_PATH_MAX + 1];
  const uint8_t *data;
  uint8_t flags;
  uint8_t pathlen;
  uint16_t datalen;

  if (frame->payload_len < IPC_ASSET_PATH)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
      return;
    }

  flags   = frame->payload[IPC_ASSET_FLAGS];
  pathlen = frame->payload[IPC_ASSET_PATHLEN];

  if (pathlen == 0 || pathlen > IPC_ASSET_PATH_MAX ||
      frame->payload_len < (uint16_t)(IPC_ASSET_PATH + pathlen))
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
      return;
    }

  memcpy(path, &frame->payload[IPC_ASSET_PATH], pathlen);
  path[pathlen] = '\0';

  data    = &frame->payload[IPC_ASSET_PATH + pathlen];
  datalen = frame->payload_len - (uint16_t)(IPC_ASSET_PATH + pathlen);

  if ((flags & IPC_ASSET_FIRST) != 0)
    {
      if (g_asset_fd >= 0)
        {
          close(g_asset_fd);
        }

      g_asset_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (g_asset_fd < 0)
        {
          /* The directory of the file may be absent on a new board. */

          ipc_asset_mkdir(path);
          g_asset_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
    }

  if (g_asset_fd < 0)
    {
      ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
      return;
    }

  if (datalen > 0 && write(g_asset_fd, data, datalen) != (ssize_t)datalen)
    {
      close(g_asset_fd);
      g_asset_fd = -1;
      ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
      return;
    }

  if ((flags & IPC_ASSET_LAST) != 0)
    {
      close(g_asset_fd);
      g_asset_fd = -1;
    }

  ipc_ack(ctx, frame->corr_id);
}
#endif

static void ipc_on_frame(void *arg, const struct ipc_frame_s *frame)
{
  struct ipc_ctx_s *ctx = (struct ipc_ctx_s *)arg;

  switch (frame->opcode)
    {
      case IPC_OP_SET_TIME:
        ipc_set_time(ctx, frame);
        break;

      case IPC_OP_SET_TEXT:
        ipc_set_text(ctx, frame);
        break;

#ifdef CONFIG_FS_SMARTFS
      case IPC_OP_WRITE_ASSET:
        ipc_write_asset(ctx, frame);
        break;
#endif

      case IPC_OP_SET_ANIM:
        ipc_animate(ctx, frame);
        break;

      case IPC_OP_ANIM_SPEED:
        if (frame->payload_len != IPC_SPEED_LEN)
          {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
          }
        else if (hazk03_display_animspeed(
                     frame->payload[IPC_SPEED_PANEL],
                     ipc_get_u16(&frame->payload[IPC_SPEED_PERIOD]),
                     frame->payload[IPC_SPEED_STEP]) < 0)
          {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
          }
        else
          {
            ipc_ack(ctx, frame->corr_id);
          }
        break;

      case IPC_OP_SET_FONT:
        {
          char path[64];
          size_t plen = frame->payload_len;

          if (plen == 0 || plen >= sizeof(path))
            {
              ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
              break;
            }

          memcpy(path, frame->payload, plen);
          path[plen] = '\0';

          if (fontext_load(path) < 0)
            {
              ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
            }
          else
            {
              ipc_ack(ctx, frame->corr_id);
            }
        }
        break;

      case IPC_OP_CLEAR:
        if (frame->payload_len == 0)
          {
            hazk03_display_clear(HAZK03_PANEL_MAIN);
            hazk03_display_clear(HAZK03_PANEL_SUB);
            ipc_ack(ctx, frame->corr_id);
          }
        else if (frame->payload_len != 1)
          {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
          }
        else if (frame->payload[0] > IPC_PANEL_SUB)
          {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
          }
        else
          {
            hazk03_display_clear(frame->payload[0] == IPC_PANEL_SUB ?
                                 HAZK03_PANEL_SUB : HAZK03_PANEL_MAIN);
            ipc_ack(ctx, frame->corr_id);
          }
        break;

      case IPC_OP_ANIM_STOP:
        hazk03_display_animstop(HAZK03_PANEL_MAIN);
        hazk03_display_animstop(HAZK03_PANEL_SUB);
        ipc_ack(ctx, frame->corr_id);
        break;

      case IPC_OP_SET_PIXELS:
        ipc_set_pixels(ctx, frame);
        break;

      case IPC_OP_SET_TEMPOFF:
        ipc_set_tempoff(ctx, frame);
        break;

      case IPC_OP_SET_SLEEP:
        ipc_set_sleep(ctx, frame);
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
