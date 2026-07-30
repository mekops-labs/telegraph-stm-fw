/* SPDX-License-Identifier: Apache-2.0 */

/* The server of the protocol that connects this board to the edge MCU.
 *
 * Note: the UART of the edge MCU also carries the serial console. Thus only a
 * build without a console starts this task. Refer to docs/ipc-protocol.md.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <nuttx/clock.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/kthread.h>

#include <telegraph/ipc.h>

#include "fontext.h"
#include "hazk03.h"
#include "version.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define IPC_DEVPATH "/dev/ttyS0"

/* The edge MCU takes the rate of the link from the header of the protocol.
 * This board takes it from its configuration, thus the two values must agree.
 */

#if CONFIG_USART1_BAUD != IPC_BAUD
#error "CONFIG_USART1_BAUD differs from IPC_BAUD"
#endif

#define IPC_STACKSIZE 2048

/* This priority is above the scan loop of the display. The task waits on the
 * UART almost all of the time. Thus it takes little CPU time from the scan.
 */

#define IPC_PRIORITY 100

/* The receive line is idle after this period. A frame at 115200 baud takes
 * near 90 ms for the maximum payload. This value is for the short frames of
 * the control traffic.
 */

#define IPC_IDLE_MS 20

#define IPC_READ_CHUNK 128

/* The main panel holds 11 characters, and the sub panel holds 3 characters.
 * A longer text loses its end.
 */

#define IPC_TEXT_MAX 64

/* A stat of one entry of a list takes the directory, a separator and the name
 * of the entry.
 *
 * Note: a name longer than this value gives a size of 0 in the reply, because
 * the stat of the truncated path fails. SmartFS holds 32 characters.
 */

#define IPC_FS_NAME_MAX 64
#define IPC_FS_FULLPATH_MAX (IPC_ASSET_PATH_MAX + 1 + IPC_FS_NAME_MAX + 1)

/* The CDC/ACM class of the USB host registers a serial device under this name,
 * one for each channel.
 */

#define IPC_USB_DEVFMT "/dev/ttyACM%u"
#define IPC_USB_DEVPATH_MAX 16
#define IPC_USB_NOTE_MAX 40
#define IPC_USB_ARG_MAX 4

/* One task waits on each channel. The priority is below the scan loop of the
 * display, and the task waits on its device almost all of the time.
 */

#define IPC_CHAN_PRIORITY 80
#define IPC_CHAN_STACKSIZE 1024
#define IPC_CHAN_CHUNK 64

/* The ring holds what a channel sends between two runs of the server. A burst
 * of a device at 115200 baud gives near 1200 bytes.
 */

#define IPC_CHAN_RING 2048u

/* The frames of a channel that one run of the server sends. The limit keeps a
 * talkative device from holding the link away from a request.
 */

#define IPC_CHAN_DRAIN 8

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ipc_ctx_s {
    int fd;
    struct ipc_parser_s parser;
    uint8_t tx[IPC_FRAME_MAX];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ipc_ctx_s *g_ipc;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* The count of the frames that a write of the link abandoned part way. */

static uint32_t g_link_lost;

static void ipc_send(const struct ipc_ctx_s *ctx, int len) {
    const uint8_t *p = ctx->tx;

    if (len <= 0) {
        return;
    }

    while (len > 0) {
        ssize_t n = write(ctx->fd, p, (size_t)len);

        if (n <= 0) {
            g_link_lost++;
            return;
        }

        p += n;
        len -= (int)n;
    }
}

/* Give the number of the frames that the receive buffer still accepts. */

static uint8_t ipc_credits(const struct ipc_ctx_s *ctx) {
    int pending = 0;
    int free_bytes;

    if (ioctl(ctx->fd, FIONREAD, (unsigned long)&pending) < 0) {
        pending = 0;
    }

    free_bytes = CONFIG_USART1_RXBUFSIZE - pending;

    if (free_bytes < 0) {
        free_bytes = 0;
    }

    free_bytes /= (int)IPC_CREDIT_UNIT;

    return (free_bytes > 255) ? 255 : (uint8_t)free_bytes;
}

#ifdef CONFIG_FS_SMARTFS
/* The file of the assets that a transfer holds open. */

static int g_asset_fd = -1;
#endif

/* The payload of a reply that holds more than a few bytes: a list of a
 * directory, a part of a file, the devices of the USB port. The buffer is
 * static, because the stack of the task holds no room for it.
 */

static uint8_t g_reply[IPC_REPLY_MAX];

static void ipc_ack(struct ipc_ctx_s *ctx, uint16_t corr_id) {
    ipc_send(ctx, ipc_encode_ack(ctx->tx, sizeof(ctx->tx), corr_id,
                                 ipc_credits(ctx)));
}

static void ipc_nack(struct ipc_ctx_s *ctx, uint16_t corr_id, uint8_t error) {
    ipc_send(ctx, ipc_encode_nack(ctx->tx, sizeof(ctx->tx), corr_id, error));
}

/* Give the names of the assets of one kind. The reply carries the opcode of
 * the request, thus the caller matches it as it matches any other reply.
 */

static void ipc_list(struct ipc_ctx_s *ctx, const struct ipc_frame_s *frame,
                     const char *dir, const char *ext) {
    char list[IPC_LIST_MAX];
    size_t used;

    used = hazk03_asset_list(list, sizeof(list), dir, ext);

    ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), frame->opcode,
                             frame->corr_id, list, (uint16_t)used));
}

/* Transmit a log line. The frame is a push, thus it carries no request. */

static void ipc_log(struct ipc_ctx_s *ctx, const char *text) {
    size_t len = strlen(text);

    if (len > IPC_TEXT_MAX) {
        len = IPC_TEXT_MAX;
    }

    ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), IPC_OP_LOG,
                             IPC_CORR_ID_PUSH, text, (uint16_t)len));
}

static void ipc_send_state(struct ipc_ctx_s *ctx, uint16_t corr_id) {
    uint8_t payload[IPC_STATE_LEN + IPC_FWVER_MAX];
    const struct ipc_stats_s *st = &ctx->parser.stats;
    size_t vlen = strlen(HAZK03_VERSION);

    ipc_put_u32(&payload[IPC_STATE_TIME], (uint32_t)time(NULL));
    ipc_put_u16(&payload[IPC_STATE_TEMP],
                (uint16_t)hazk03_display_temperature());
    ipc_put_u16(&payload[IPC_STATE_FRAMES], (uint16_t)st->frames);
    ipc_put_u16(&payload[IPC_STATE_CRC_ERR], (uint16_t)st->crc_errors);

    payload[IPC_STATE_RESYNC] = (uint8_t)st->resyncs;
    payload[IPC_STATE_VERSION] = IPC_PROTO_VERSION;

    if (vlen > IPC_FWVER_MAX) {
        vlen = IPC_FWVER_MAX;
    }

    memcpy(&payload[IPC_STATE_FWVER], HAZK03_VERSION, vlen);

    ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), IPC_OP_STATE, corr_id,
                             payload, (uint16_t)(IPC_STATE_LEN + vlen)));
}

static void ipc_set_time(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame) {
    struct timespec ts;

    if (frame->payload_len != IPC_SET_TIME_LEN &&
        frame->payload_len != IPC_SET_TIME_TZ_LEN) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    ts.tv_sec = (time_t)ipc_get_u32(&frame->payload[IPC_SET_TIME_UTC]);
    ts.tv_nsec = 0;

    /* The panels show the local time. The RTC keeps UTC, thus the offset stays
     * with the display.
     */

    if (frame->payload_len == IPC_SET_TIME_TZ_LEN) {
        hazk03_display_utcoffset(
            (int16_t)ipc_get_u16(&frame->payload[IPC_SET_TIME_OFFSET]));
    }

    /* The system clock is the DS3231, and a battery holds that device. Thus
     * this value stays correct after a power interruption.
     */

    if (clock_settime(CLOCK_REALTIME, &ts) < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    ipc_ack(ctx, frame->corr_id);
}

/* Take the panel from the first byte of a payload. */

static int ipc_panel_of(const struct ipc_frame_s *frame) {
    if (frame->payload_len == 0 || frame->payload[0] > IPC_PANEL_SUB) {
        return -1;
    }

    return (frame->payload[0] == IPC_PANEL_SUB) ? HAZK03_PANEL_SUB
                                                : HAZK03_PANEL_MAIN;
}

static void ipc_set_text(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame) {
    int panel = ipc_panel_of(frame);

    char text[IPC_TEXT_MAX];
    uint8_t align = IPC_ALIGN_CENTRE;
    uint8_t valign = IPC_VALIGN_MIDDLE;
    size_t len = 0;

    if (panel < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    /* The byte after the panel carries the attributes. A payload of the panel
     * alone clears that panel, thus it needs no such byte.
     */

    if (frame->payload_len > IPC_TEXT_ATTRS) {
        uint8_t attrs = frame->payload[IPC_TEXT_ATTRS];

        if ((attrs & ~IPC_TEXT_ATTR_MASK) != 0) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
            return;
        }

        align = attrs & IPC_ALIGN_MASK;
        valign = (attrs & IPC_VALIGN_MASK) >> IPC_VALIGN_SHIFT;

        if (align > IPC_ALIGN_RIGHT || valign > IPC_VALIGN_BOTTOM) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
            return;
        }

        len = frame->payload_len - IPC_TEXT_BODY;
    }

    if (len > sizeof(text)) {
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
                           const struct ipc_frame_s *frame) {
    uint8_t digits;
    uint8_t panels;

    if (frame->payload_len != IPC_SET_BRIGHT_LEN &&
        frame->payload_len != IPC_SET_BRIGHT2_LEN) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    digits = frame->payload[0];
    panels = (frame->payload_len == IPC_SET_BRIGHT2_LEN) ? frame->payload[1]
                                                         : digits;

    if (digits > IPC_BRIGHT_MAX || panels > IPC_BRIGHT_MAX) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    hazk03_display_brightness(digits, panels);
    ipc_ack(ctx, frame->corr_id);
}

static void ipc_animate(struct ipc_ctx_s *ctx,
                        const struct ipc_frame_s *frame) {
    int panel = ipc_panel_of(frame);
    uint8_t flags;
    bool text;
    bool file;
    int srcw;
    int srch;
    size_t srclen;
    int ret;

    if (panel < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    if (frame->payload_len <= IPC_ANIM_BODY) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    flags = frame->payload[IPC_ANIM_FLAGS];

    if ((flags & ~IPC_ANIM_FLAG_MASK) != 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    text = (flags & IPC_ANIM_TEXT) != 0;
    file = (flags & IPC_ANIM_FILE) != 0;
    srcw = frame->payload[IPC_ANIM_SRCW];
    srch = frame->payload[IPC_ANIM_SRCH];
    srclen = frame->payload_len - IPC_ANIM_BODY;

    if (!text && !file) {
        /* The pixels must match the size of the source. */

        if (srcw == 0 || srch == 0 ||
            srclen != (size_t)(((srcw + 7) / 8) * srch)) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
            return;
        }
    }

    ret = hazk03_display_animate(
        panel, frame->payload[IPC_ANIM_X], frame->payload[IPC_ANIM_Y],
        frame->payload[IPC_ANIM_W], frame->payload[IPC_ANIM_H],
        (flags & IPC_ANIM_VERTICAL) != 0,
        ipc_get_u16(&frame->payload[IPC_ANIM_PERIOD]),
        frame->payload[IPC_ANIM_STEP], text, file, srcw, srch,
        &frame->payload[IPC_ANIM_BODY], srclen);

    if (ret < 0) {
        ipc_nack(ctx, frame->corr_id,
                 (ret == -E2BIG) ? IPC_ERR_BAD_LENGTH : IPC_ERR_BAD_PAYLOAD);
        return;
    }

    ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_pixels(struct ipc_ctx_s *ctx,
                           const struct ipc_frame_s *frame) {
    int panel = ipc_panel_of(frame);
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
    uint16_t need;

    if (panel < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    if (frame->payload_len < IPC_PIX_HEADER) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    x = frame->payload[IPC_PIX_X];
    y = frame->payload[IPC_PIX_Y];
    w = frame->payload[IPC_PIX_W];
    h = frame->payload[IPC_PIX_H];

    if (w == 0 || h == 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    need = (uint16_t)IPC_PIX_HEADER + (uint16_t)(((w + 7) / 8) * h);

    if (frame->payload_len != need) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    hazk03_display_pixels(panel, x, y, w, h, &frame->payload[IPC_PIX_BITS]);
    ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_tempoff(struct ipc_ctx_s *ctx,
                            const struct ipc_frame_s *frame) {
    if (frame->payload_len != IPC_SET_TEMPOFF_LEN) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    hazk03_display_tempoffset((int16_t)ipc_get_u16(frame->payload));
    ipc_ack(ctx, frame->corr_id);
}

static void ipc_set_sleep(struct ipc_ctx_s *ctx,
                          const struct ipc_frame_s *frame) {
    uint16_t start;
    uint16_t end;

    if (frame->payload_len != IPC_SET_SLEEP_LEN) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    start = ipc_get_u16(&frame->payload[IPC_SLEEP_START]);
    end = ipc_get_u16(&frame->payload[IPC_SLEEP_END]);

    /* A minute above the day is invalid. The value that stops the function is
     * the one exception.
     */

    if ((start >= IPC_MINUTES_PER_DAY && start != IPC_SLEEP_OFF) ||
        (end >= IPC_MINUTES_PER_DAY && end != IPC_SLEEP_OFF)) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    hazk03_display_sleep(start, end);
    ipc_ack(ctx, frame->corr_id);
}

#ifdef CONFIG_FS_SMARTFS
/* Make the directory of a path. A path with no directory does nothing. */

/* Test a path of a storage opcode against the roots the link reaches. A root
 * matches the whole first element of the path, thus "/assetsx" is outside it.
 */

static bool ipc_path_ok(const char *path) {
    static const char *const roots[] = {IPC_ROOT_ASSETS, IPC_ROOT_MEDIA};

    if (strstr(path, "..") != NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        size_t len = strlen(roots[i]);

        if (strncmp(path, roots[i], len) == 0 &&
            (path[len] == '\0' || path[len] == '/')) {
            return true;
        }
    }

    return false;
}

/* Copy the path that ends the payload into dst. The caller gives the offset of
 * the first byte of the path.
 */

static bool ipc_take_path(const struct ipc_frame_s *frame, uint16_t offset,
                          char *dst, size_t size) {
    uint16_t len;

    if (frame->payload_len <= offset) {
        return false;
    }

    len = frame->payload_len - offset;

    if (len >= size) {
        return false;
    }

    memcpy(dst, &frame->payload[offset], len);
    dst[len] = '\0';

    return ipc_path_ok(dst);
}

/* Give the entries of a directory, from the ordinal that the request names.
 * The reply holds as many entries as one frame carries.
 */

static void ipc_fs_list(struct ipc_ctx_s *ctx,
                        const struct ipc_frame_s *frame) {
    char path[IPC_ASSET_PATH_MAX + 1];
    uint8_t *reply = g_reply;
    uint16_t used = IPC_FS_LIST_PATH;
    uint16_t cursor;
    uint16_t ordinal = 0;
    DIR *dir;

    if (frame->payload_len < IPC_FS_LIST_PATH) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    cursor = ipc_get_u16(&frame->payload[IPC_FS_LIST_INDEX]);

    if (!ipc_take_path(frame, IPC_FS_LIST_PATH, path, sizeof(path))) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    dir = opendir(path);
    if (dir == NULL) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    for (;;) {
        struct dirent *entry = readdir(dir);
        size_t namelen;
        struct stat st;
        char full[IPC_FS_FULLPATH_MAX];

        if (entry == NULL) {
            cursor = IPC_FS_INDEX_END;
            break;
        }

        if (ordinal++ < cursor) {
            continue;
        }

        namelen = strlen(entry->d_name);

        if (used + IPC_FS_ENTRY_NAME + namelen > IPC_FS_REPLY_MAX) {
            cursor = (uint16_t)(ordinal - 1);
            break;
        }

        /* The size comes from a stat of the entry. A directory reports 0. */

        st.st_size = 0;
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        stat(full, &st);

        reply[used + IPC_FS_ENTRY_KIND] = (entry->d_type == DTYPE_DIRECTORY)
                                              ? IPC_FS_KIND_DIR
                                              : IPC_FS_KIND_FILE;

        ipc_put_u32(&reply[used + IPC_FS_ENTRY_SIZE],
                    (entry->d_type == DTYPE_DIRECTORY) ? 0u
                                                       : (uint32_t)st.st_size);

        reply[used + IPC_FS_ENTRY_NAMELEN] = (uint8_t)namelen;
        memcpy(&reply[used + IPC_FS_ENTRY_NAME], entry->d_name, namelen);

        used += (uint16_t)(IPC_FS_ENTRY_NAME + namelen);
    }

    closedir(dir);

    ipc_put_u16(&reply[IPC_FS_LIST_INDEX], cursor);

    ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), frame->opcode,
                             frame->corr_id, reply, used));
}

/* Give one part of a file. A reply of the offset alone states that the offset
 * is at or past the end of the file.
 */

static void ipc_fs_read(struct ipc_ctx_s *ctx,
                        const struct ipc_frame_s *frame) {
    char path[IPC_ASSET_PATH_MAX + 1];
    uint8_t *reply = g_reply;
    uint32_t offset;
    uint16_t length;
    ssize_t got;
    int fd;

    if (frame->payload_len < IPC_FS_READ_PATH) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    offset = ipc_get_u32(&frame->payload[IPC_FS_READ_OFFSET]);
    length = ipc_get_u16(&frame->payload[IPC_FS_READ_LENGTH]);

    if (length > IPC_FS_READ_MAX) {
        length = IPC_FS_READ_MAX;
    }

    if (!ipc_take_path(frame, IPC_FS_READ_PATH, path, sizeof(path))) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    if (lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset) {
        close(fd);
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    got = read(fd, &reply[IPC_FS_READ_DATA], length);
    close(fd);

    if (got < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    ipc_put_u32(&reply[IPC_FS_READ_OFFSET], offset);

    ipc_send(ctx,
             ipc_encode(ctx->tx, sizeof(ctx->tx), frame->opcode, frame->corr_id,
                        reply, (uint16_t)(IPC_FS_READ_DATA + (size_t)got)));
}

static void ipc_fs_delete(struct ipc_ctx_s *ctx,
                          const struct ipc_frame_s *frame) {
    char path[IPC_ASSET_PATH_MAX + 1];

    if (!ipc_take_path(frame, 0, path, sizeof(path))) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    /* A directory needs its own call, and the caller does not state the kind
     * of the entry.
     */

    if (unlink(path) < 0 && rmdir(path) < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    ipc_ack(ctx, frame->corr_id);
}

static void ipc_fs_mkdir(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame) {
    char path[IPC_ASSET_PATH_MAX + 1];

    if (!ipc_take_path(frame, 0, path, sizeof(path))) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    if (mkdir(path, 0777) < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    ipc_ack(ctx, frame->corr_id);
}

/* cppcheck-suppress constParameterPointer
 * The function writes through path via the alias strrchr() returns, which
 * cppcheck does not trace back to this parameter. A const path would still
 * compile, because the standard strrchr() prototype returns a non-const
 * char* regardless of its own argument's constness, but path is genuinely
 * mutated here.
 */

static void ipc_asset_mkdir(char *path) {
    char *slash = strrchr(path, '/');

    if (slash == NULL || slash == path) {
        return;
    }

    *slash = '\0';
    mkdir(path, 0777);
    *slash = '/';
}

static void ipc_write_asset(struct ipc_ctx_s *ctx,
                            const struct ipc_frame_s *frame) {
    char path[IPC_ASSET_PATH_MAX + 1];
    const uint8_t *data;
    uint8_t flags;
    uint8_t pathlen;
    uint16_t datalen;

    if (frame->payload_len < IPC_ASSET_PATH) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    flags = frame->payload[IPC_ASSET_FLAGS];
    pathlen = frame->payload[IPC_ASSET_PATHLEN];

    if (pathlen == 0 || pathlen > IPC_ASSET_PATH_MAX ||
        frame->payload_len < (uint16_t)(IPC_ASSET_PATH + pathlen)) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    memcpy(path, &frame->payload[IPC_ASSET_PATH], pathlen);
    path[pathlen] = '\0';

    if (!ipc_path_ok(path)) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    data = &frame->payload[IPC_ASSET_PATH + pathlen];
    datalen = frame->payload_len - (uint16_t)(IPC_ASSET_PATH + pathlen);

    if ((flags & IPC_ASSET_FIRST) != 0) {
        if (g_asset_fd >= 0) {
            close(g_asset_fd);
        }

        g_asset_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (g_asset_fd < 0) {
            /* The directory of the file may be absent on a new board. */

            ipc_asset_mkdir(path);
            g_asset_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
    }

    if (g_asset_fd < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    if (datalen > 0 && write(g_asset_fd, data, datalen) != (ssize_t)datalen) {
        close(g_asset_fd);
        g_asset_fd = -1;
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    if ((flags & IPC_ASSET_LAST) != 0) {
        close(g_asset_fd);
        g_asset_fd = -1;
    }

    ipc_ack(ctx, frame->corr_id);
}
#endif

#ifdef CONFIG_USBHOST_CDCACM
/* The serial devices of the USB port that the board holds open, and the channel
 * that it follows. A device stays open, because a reader of it waits in a read
 * that only its own bytes end.
 */

static int g_usb_fds[IPC_USB_CHANNELS];
static bool g_usb_reader[IPC_USB_CHANNELS];
static int g_usb_fd = -1;
static uint8_t g_usb_chan;
static bool g_usb_sub;

/* The sequence of the last write that the board took. A repeat of that write
 * carries the same value, and the board then adds nothing to the stream.
 */

static uint8_t g_usb_seq;
static bool g_usb_seq_valid;

/* What the reads of a channel gave. The counts tell a channel that carries
 * nothing from one whose bytes the link then loses.
 */

static uint32_t g_usb_bytes;
static uint32_t g_usb_reads;

/* The bytes of a followed channel wait here, between the reader of that channel
 * and the server that sends them. One task fills the ring and one empties it,
 * thus the two indexes need no lock.
 */

static uint8_t g_chan_ring[IPC_CHAN_RING];
static volatile uint16_t g_chan_head;
static volatile uint16_t g_chan_tail;
static uint32_t g_chan_over;

static uint16_t ipc_chan_next(uint16_t at) {
    return (uint16_t)((at + 1u) % IPC_CHAN_RING);
}

static void ipc_chan_put(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint16_t next = ipc_chan_next(g_chan_head);

        if (next == g_chan_tail) {
            g_chan_over++;
            return;
        }

        g_chan_ring[g_chan_head] = data[i];
        g_chan_head = next;
    }
}

static size_t ipc_chan_take(uint8_t *data, size_t max) {
    size_t got = 0;

    while (got < max && g_chan_tail != g_chan_head) {
        data[got++] = g_chan_ring[g_chan_tail];
        g_chan_tail = ipc_chan_next(g_chan_tail);
    }

    return got;
}

static void ipc_usb_devpath(char *path, size_t size, uint8_t channel) {
    snprintf(path, size, IPC_USB_DEVFMT, channel);
}

/* Wait on one channel, and give what it sends to the ring.
 *
 * Note: the class of the USB host takes one packet from the device for each run
 * of its own work, and it schedules that work again after a delay of at least
 * one tick. A read that waits replaces that delay with immediate work, thus the
 * host asks the device for a packet as fast as the device fills one. A reader
 * that cannot wait leaves the delay in place, and a device that talks faster
 * than the delay allows then drops what it cannot hand over. Thus this task
 * exists: the server of the protocol serves the link, and it can never wait
 * here.
 */

static int ipc_chan_reader(int argc, char **argv) {
    uint8_t buf[IPC_CHAN_CHUNK];
    uint8_t channel;

    (void)argc;

    channel = (uint8_t)atoi(argv[1]);

    for (;;) {
        ssize_t got = read(g_usb_fds[channel], buf, sizeof(buf));

        if (got <= 0) {
            /* Nothing here recovers a channel whose device is gone. A pause
             * keeps this task from spinning above the scan of the display.
             */

            usleep(IPC_IDLE_MS * USEC_PER_MSEC);
            continue;
        }

        g_usb_reads++;
        g_usb_bytes += (uint32_t)got;

        /* The bytes of a channel that no one follows end here. Draining them
         * keeps its device from holding output that no one asked for.
         */

        if (g_usb_sub && g_usb_chan == channel) {
            ipc_chan_put(buf, (size_t)got);
        }
    }

    return 0;
}

/* Open the serial device of a channel, and keep it open. The reader of a
 * channel waits in a read of it, thus no other task closes that device.
 */

static int ipc_usb_open(uint8_t channel) {
    char arg[IPC_USB_ARG_MAX];

    if (channel >= IPC_USB_CHANNELS) {
        return -1;
    }

    if (g_usb_fds[channel] < 0) {
        char path[IPC_USB_DEVPATH_MAX];

        ipc_usb_devpath(path, sizeof(path), channel);

        /* The reader of this channel waits here, thus the device carries no
         * O_NONBLOCK.
         */

        g_usb_fds[channel] = open(path, O_RDWR);
    }

    if (g_usb_fds[channel] < 0) {
        return -1;
    }

    if (!g_usb_reader[channel]) {
        char *argv[2];

        snprintf(arg, sizeof(arg), "%u", channel);

        argv[0] = arg;
        argv[1] = NULL;

        if (kthread_create("usbchan", IPC_CHAN_PRIORITY, IPC_CHAN_STACKSIZE,
                           ipc_chan_reader, argv) < 0) {
            return -1;
        }

        g_usb_reader[channel] = true;
    }

    if (g_usb_fd != g_usb_fds[channel]) {
        /* Another device starts another stream, thus the sequence of the
         * previous one names no write of this one.
         */

        g_usb_seq_valid = false;
    }

    g_usb_fd = g_usb_fds[channel];
    g_usb_chan = channel;

    return g_usb_fd;
}

/* Give one record for each device of the USB port. A serial device carries the
 * number of its channel, and the storage carries none.
 */

static void ipc_usb_list(struct ipc_ctx_s *ctx,
                         const struct ipc_frame_s *frame) {
    uint8_t *reply = g_reply;
    uint16_t used = 0;

    for (uint8_t channel = 0; channel < IPC_USB_CHANNELS; channel++) {
        char path[IPC_USB_DEVPATH_MAX];
        struct stat st;
        size_t namelen;

        ipc_usb_devpath(path, sizeof(path), channel);

        if (stat(path, &st) < 0) {
            continue;
        }

        namelen = strlen(path);

        reply[used + IPC_USB_DEV_CHANNEL] = channel;
        reply[used + IPC_USB_DEV_KIND] = IPC_USB_KIND_SERIAL;
        reply[used + IPC_USB_DEV_NAMELEN] = (uint8_t)namelen;
        memcpy(&reply[used + IPC_USB_DEV_NAME], path, namelen);

        used += (uint16_t)(IPC_USB_DEV_NAME + namelen);
    }

#ifdef CONFIG_USBHOST_MSC
    {
        struct stat st;

        if (stat(IPC_ROOT_MEDIA, &st) == 0) {
            size_t namelen = strlen(IPC_ROOT_MEDIA);

            reply[used + IPC_USB_DEV_CHANNEL] = IPC_USB_NO_CHANNEL;
            reply[used + IPC_USB_DEV_KIND] = IPC_USB_KIND_STORAGE;
            reply[used + IPC_USB_DEV_NAMELEN] = (uint8_t)namelen;
            memcpy(&reply[used + IPC_USB_DEV_NAME], IPC_ROOT_MEDIA, namelen);

            used += (uint16_t)(IPC_USB_DEV_NAME + namelen);
        }
    }
#endif

    ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), IPC_OP_USB_DEVS,
                             frame->corr_id, reply, used));

#ifdef CONFIG_USBHOST_CDCACM
    {
        char note[IPC_TEXT_MAX];

        snprintf(note, sizeof(note),
                 "usb: bytes=%lu reads=%lu over=%lu lost=%lu",
                 (unsigned long)g_usb_bytes, (unsigned long)g_usb_reads,
                 (unsigned long)g_chan_over, (unsigned long)g_link_lost);
        ipc_log(ctx, note);
    }
#endif
}

static void ipc_usb_write(struct ipc_ctx_s *ctx,
                          const struct ipc_frame_s *frame) {
    const uint8_t *data;
    uint16_t datalen;
    uint8_t seq;
    int fd;

    if (frame->payload_len < IPC_USB_WRITE_DATA) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    fd = ipc_usb_open(frame->payload[IPC_USB_CHANNEL]);
    if (fd < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    /* A repeat of a write carries the sequence of that write. The stream takes
     * the bytes one time, and the sender takes the reply it missed.
     */

    seq = frame->payload[IPC_USB_WRITE_SEQ];

    if (g_usb_seq_valid && g_usb_seq == seq) {
        char note[IPC_USB_NOTE_MAX];

        /* A repeat is rare, thus a line for each one costs nothing and it
         * shows that the stream took the bytes one time.
         */

        snprintf(note, sizeof(note), "usb: repeat seq=%u took no bytes", seq);
        ipc_log(ctx, note);

        ipc_ack(ctx, frame->corr_id);
        return;
    }

    data = &frame->payload[IPC_USB_WRITE_DATA];
    datalen = frame->payload_len - IPC_USB_WRITE_DATA;

    if (datalen > 0 && write(fd, data, datalen) != (ssize_t)datalen) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    g_usb_seq = seq;
    g_usb_seq_valid = true;

    ipc_ack(ctx, frame->corr_id);
}

static void ipc_usb_sub(struct ipc_ctx_s *ctx,
                        const struct ipc_frame_s *frame) {
    if (frame->payload_len != IPC_USB_SUB_LEN) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    if (frame->payload[IPC_USB_STATE] == 0) {
        g_usb_sub = false;
        ipc_ack(ctx, frame->corr_id);
        return;
    }

    if (ipc_usb_open(frame->payload[IPC_USB_CHANNEL]) < 0) {
        ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        return;
    }

    g_usb_sub = true;
    ipc_ack(ctx, frame->corr_id);
}

/* Send what the channel holds. The frame is a push, thus it carries no
 * request.
 */

static void ipc_usb_push(struct ipc_ctx_s *ctx) {
    uint8_t *payload = g_reply;
    size_t got;

    got = ipc_chan_take(&payload[IPC_USB_PUSH_DATA], IPC_USB_READ_MAX);

    if (got == 0) {
        return;
    }

    payload[IPC_USB_CHANNEL] = g_usb_chan;

    ipc_send(ctx, ipc_encode(ctx->tx, sizeof(ctx->tx), IPC_OP_USB_DATA,
                             IPC_CORR_ID_PUSH, payload,
                             (uint16_t)(IPC_USB_PUSH_DATA + got)));
}
#endif

static void ipc_on_frame(void *arg, const struct ipc_frame_s *frame) {
    struct ipc_ctx_s *ctx = (struct ipc_ctx_s *)arg;

    switch (frame->opcode) {
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

    case IPC_OP_FS_LIST:
        ipc_fs_list(ctx, frame);
        break;

    case IPC_OP_FS_READ:
        ipc_fs_read(ctx, frame);
        break;

    case IPC_OP_FS_DELETE:
        ipc_fs_delete(ctx, frame);
        break;

    case IPC_OP_FS_MKDIR:
        ipc_fs_mkdir(ctx, frame);
        break;
#endif

    case IPC_OP_SET_ANIM:
        if (frame->payload_len == 0) {
            ipc_list(ctx, frame, HAZK03_ANIM_DIR, HAZK03_ANIM_EXT);
        } else {
            ipc_animate(ctx, frame);
        }
        break;

    case IPC_OP_ANIM_SPEED:
        if (frame->payload_len != IPC_SPEED_LEN) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        } else if (hazk03_display_animspeed(
                       frame->payload[IPC_SPEED_PANEL],
                       ipc_get_u16(&frame->payload[IPC_SPEED_PERIOD]),
                       frame->payload[IPC_SPEED_STEP]) < 0) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        } else {
            ipc_ack(ctx, frame->corr_id);
        }
        break;

    case IPC_OP_SET_FONT: {
        char path[64];

        if (frame->payload_len == 0) {
            ipc_list(ctx, frame, HAZK03_FONT_DIR, HAZK03_FONT_EXT);
            break;
        }

        if (hazk03_asset_path(path, sizeof(path), HAZK03_FONT_DIR,
                              (const char *)frame->payload, frame->payload_len,
                              HAZK03_FONT_EXT) < 0) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
            break;
        }

        if (fontext_load(path) < 0) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_FAILED);
        } else {
            ipc_ack(ctx, frame->corr_id);
        }
    } break;

    case IPC_OP_CLEAR:
        if (frame->payload_len == 0) {
            hazk03_display_clear(HAZK03_PANEL_MAIN);
            hazk03_display_clear(HAZK03_PANEL_SUB);
            ipc_ack(ctx, frame->corr_id);
        } else if (frame->payload_len != 1) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_LENGTH);
        } else if (frame->payload[0] > IPC_PANEL_SUB) {
            ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        } else {
            hazk03_display_clear(frame->payload[0] == IPC_PANEL_SUB
                                     ? HAZK03_PANEL_SUB
                                     : HAZK03_PANEL_MAIN);
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

#ifdef CONFIG_USBHOST_CDCACM
    case IPC_OP_USB_LIST:
        ipc_usb_list(ctx, frame);
        break;

    case IPC_OP_USB_WRITE:
        ipc_usb_write(ctx, frame);
        break;

    case IPC_OP_USB_SUB:
        ipc_usb_sub(ctx, frame);
        break;
#endif

    default:
        ipc_nack(ctx, frame->corr_id, IPC_ERR_BAD_OPCODE);
        break;
    }
}

static int ipc_server(int argc, char *argv[]) {
    struct ipc_ctx_s *ctx = g_ipc;
    uint8_t buf[IPC_READ_CHUNK];
    char hse[HAZK03_HSE_REPORT_MAX];

#ifdef CONFIG_USBHOST_CDCACM
    for (uint8_t i = 0; i < IPC_USB_CHANNELS; i++) {
        g_usb_fds[i] = -1;
    }
#endif

    ipc_log(ctx, "telegraph ipc ready");

    hazk03_hse_probe(hse, sizeof(hse));
    ipc_log(ctx, hse);

    for (;;) {
        struct pollfd pfd;
        int timeout;
        int ret;
        bool worked = false;

        pfd.fd = ctx->fd;
        pfd.events = POLLIN;

        /* A wait with a limit is necessary only for a partial frame. Without
         * that condition the task blocks. Thus it does not interrupt the scan
         * loop of the display 50 times each second.
         */

        timeout = ipc_parser_pending(&ctx->parser) ? IPC_IDLE_MS : -1;

#ifdef CONFIG_USBHOST_CDCACM
        /* A followed channel waits here beside the link, thus the task needs
         * no thread of its own for it.
         */

        if (g_usb_sub && g_usb_fd >= 0) {
            /* A read of the channel follows each wait of the link, thus that
             * wait needs a limit.
             */

            timeout = IPC_IDLE_MS;
        }
#endif

        ret = poll(&pfd, 1, timeout);

        if (ret < 0) {
            /* Nothing here can recover a failed wait on the link. A pause
             * keeps this task from spinning above the scan of the display.
             */

            usleep(IPC_IDLE_MS * USEC_PER_MSEC);
            continue;
        }

        /* The link comes first. A reply that waits behind the bytes of a
         * channel is a reply that the sender gives up on.
         */

        if (ret > 0 && (pfd.revents & POLLIN) != 0) {
            ssize_t n = read(ctx->fd, buf, sizeof(buf));

            worked = true;

            if (n > 0) {
                ipc_parser_push(&ctx->parser, buf, (size_t)n, ipc_on_frame,
                                ctx);
            }
        } else if (ret == 0 && ipc_parser_pending(&ctx->parser)) {
            /* The line is idle. A false start-of-frame byte holds the parser
             * here. This call discards that candidate.
             */

            ipc_parser_timeout(&ctx->parser, ipc_on_frame, ctx);
        }

#ifdef CONFIG_USBHOST_CDCACM
        if (g_usb_sub && g_usb_fd >= 0) {
            /* A burst of a channel holds more than one frame. Sending all of
             * it here keeps the ring from filling between two waits.
             */

            for (uint8_t i = 0; i < IPC_CHAN_DRAIN; i++) {
                if (g_chan_head == g_chan_tail) {
                    break;
                }

                ipc_usb_push(ctx);
            }

            worked = true;
        }
#endif

        /* A wait that returns at once and gives this loop nothing to do would
         * otherwise spin above the scan of the display, which stops the image
         * and the link together.
         */

        if (!worked && ret > 0) {
            usleep(IPC_IDLE_MS * USEC_PER_MSEC);
        }
    }

    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hazk03_ipc_init(void) {
    struct ipc_ctx_s *ctx;
    int ret;

    ctx = (struct ipc_ctx_s *)malloc(sizeof(struct ipc_ctx_s));
    if (ctx == NULL) {
        return -ENOMEM;
    }

    ctx->fd = open(IPC_DEVPATH, O_RDWR);
    if (ctx->fd < 0) {
        free(ctx);
        return -ENODEV;
    }

    ipc_parser_init(&ctx->parser);
    g_ipc = ctx;

    ret = kthread_create("ipc", IPC_PRIORITY, IPC_STACKSIZE, ipc_server, NULL);
    if (ret < 0) {
        close(ctx->fd);
        free(ctx);
        g_ipc = NULL;
        return ret;
    }

    return OK;
}
