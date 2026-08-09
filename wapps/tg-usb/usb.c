/* SPDX-License-Identifier: Apache-2.0 */

/* The USB port of the board over HTTP.
 *
 * Note: the board owns the port; this wapp reaches it through the broker. A
 * serial device becomes a channel a client writes to and reads from, and a
 * mass storage device becomes a file tree under the media root.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <telegraph/broker.h>

/****************************************************************************
 * Definitions
 ****************************************************************************/

#define PEER_ENV "TELEGRAPH_PEER"
#define PEER_DEFAULT "usb"

#define SOCKET_ENV "TELEGRAPH_SOCKET"
#define SOCKET_DEFAULT "http"

#define REQUEST_MAX 2048u
#define REPLY_MS 3000u
#define POLL_US 10000u

/* What one channel holds between the push frames of the board and the read of
 * a client. A client that reads slower than the device writes loses the
 * oldest bytes, which is what a terminal on the other end would see anyway.
 */

#define CHANNEL_BUF 1024u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct channel_s {
    uint8_t buf[CHANNEL_BUF];
    unsigned int len;
    unsigned int dropped;
    bool subscribed;
    uint8_t seq;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_name[TG_BRK_NAME_MAX + 1] = PEER_DEFAULT;
static int g_req = -1;
static int g_rsp = -1;

static struct ipc_parser_s g_parser;
static uint8_t g_frame[IPC_FRAME_MAX];

static uint8_t g_reply[IPC_REPLY_MAX];
static unsigned int g_replyLen;
static uint8_t g_replyOp;
static bool g_gotReply;

static uint16_t g_corr = 1;

static struct channel_s g_channels[IPC_USB_CHANNELS];

/* The request of a client, and the frame this wapp builds from it. Both are
 * static: the aux stack of a wapp holds 8 KiB, and either would take an
 * eighth of it.
 */

static char g_request[REQUEST_MAX + 1];
static uint8_t g_payload[IPC_MAX_PAYLOAD];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void emit(const char *text) { write(STDOUT_FILENO, text, strlen(text)); }

static void emitf(const char *fmt, ...) {
    char line[160];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    emit(line);
}

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void nap(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = POLL_US * 1000};

    nanosleep(&ts, NULL);
}

/****************************************************************************
 * The channels
 ****************************************************************************/

/* Take the bytes of a push frame. The oldest go first when the buffer is
 * full, and the count of what went says so to the client.
 */

static void channel_fill(uint8_t index, const uint8_t *data, unsigned int len) {
    struct channel_s *ch = &g_channels[index];

    if (len >= CHANNEL_BUF) {
        ch->dropped += ch->len + (len - CHANNEL_BUF);
        memcpy(ch->buf, &data[len - CHANNEL_BUF], CHANNEL_BUF);
        ch->len = CHANNEL_BUF;
        return;
    }

    if (ch->len + len > CHANNEL_BUF) {
        unsigned int drop = ch->len + len - CHANNEL_BUF;

        memmove(ch->buf, &ch->buf[drop], ch->len - drop);
        ch->len -= drop;
        ch->dropped += drop;
    }

    memcpy(&ch->buf[ch->len], data, len);
    ch->len += len;
}

/****************************************************************************
 * The broker
 ****************************************************************************/

static void on_frame(void *arg, const struct ipc_frame_s *frame) {
    (void)arg;

    /* A push carries the bytes of a channel and answers no request, thus it
     * never becomes the reply a caller is waiting for.
     */

    if (frame->corr_id == IPC_CORR_ID_PUSH) {
        uint8_t chan;

        if (frame->opcode != IPC_OP_USB_DATA ||
            frame->payload_len <= IPC_USB_PUSH_DATA) {
            return;
        }

        chan = frame->payload[IPC_USB_CHANNEL];
        if (chan >= IPC_USB_CHANNELS) {
            return;
        }

        channel_fill(chan, &frame->payload[IPC_USB_PUSH_DATA],
                     frame->payload_len - IPC_USB_PUSH_DATA);
        return;
    }

    g_replyOp = frame->opcode;
    g_replyLen = frame->payload_len;
    if (g_replyLen > sizeof(g_reply)) {
        g_replyLen = (unsigned int)sizeof(g_reply);
    }

    memcpy(g_reply, frame->payload, g_replyLen);
    g_gotReply = true;
}

/* Read whatever the broker has and feed the parser. Called outside a request
 * as well: a push arrives whenever the device speaks, not when a client asks.
 */

static bool pump(void) {
    uint8_t buf[256];
    ssize_t got = read(g_rsp, buf, sizeof(buf));

    if (got <= 0) {
        return false;
    }

    ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
    return true;
}

/* One request to the board, and the reply it gives. */

static int ask(uint8_t opcode, const void *payload, uint16_t len) {
    int n =
        ipc_encode(g_frame, sizeof(g_frame), opcode, g_corr++, payload, len);
    uint64_t deadline;

    if (g_corr == IPC_CORR_ID_PUSH) {
        g_corr = 1;
    }

    g_gotReply = false;
    if (n < 0 || write(g_req, g_frame, (size_t)n) != n) {
        return -1;
    }

    deadline = now_ms() + REPLY_MS;
    while (!g_gotReply && now_ms() < deadline) {
        if (!pump()) {
            nap();
        }
    }

    if (!g_gotReply) {
        return -1;
    }

    return g_replyOp == IPC_OP_NACK ? -2 : 0;
}

/****************************************************************************
 * HTTP
 ****************************************************************************/

static void reply_typed(int fd, const char *status, const char *type,
                        const void *body, size_t len) {
    char head[128];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
                     "Content-Length: %u\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned)len);

    if (n > 0) {
        write(fd, head, (size_t)n);
    }

    write(fd, body, len);
}

static void reply(int fd, const char *status, const char *body) {
    reply_typed(fd, status, "application/json", body, strlen(body));
}

static const char *nack_name(uint8_t code) {
    switch (code) {
    case IPC_ERR_BAD_OPCODE:
        return "the board has no such operation";
    case IPC_ERR_BAD_LENGTH:
        return "too long";
    case IPC_ERR_BAD_PAYLOAD:
        return "a value is out of range";
    case IPC_ERR_BUSY:
        return "the board cannot take it now";
    case IPC_ERR_FAILED:
        return "the operation failed";
    case IPC_ERR_UNSUPPORTED:
        return "this build of the board has no support for it";
    default:
        return "unknown";
    }
}

static void reply_result(int fd, int rc) {
    if (rc == 0) {
        reply(fd, "200 OK", "{\"ok\":true}\n");
    } else if (rc == -2) {
        char body[128];
        uint8_t code = g_replyLen > 0 ? g_reply[0] : 0;

        snprintf(body, sizeof(body),
                 "{\"ok\":false,\"nack\":%u,\"error\":\"%s\"}\n", code,
                 nack_name(code));
        reply(fd, "409 Conflict", body);
    } else {
        reply(fd, "504 Gateway Timeout",
              "{\"ok\":false,\"error\":\"no reply\"}\n");
    }
}

/* Append text to a bounded buffer. Returns false once it no longer fits, so a
 * caller stops rather than writing a truncated record.
 */
static bool append(char *out, size_t cap, size_t *used, const char *fmt, ...) {
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(&out[*used], cap - *used, fmt, args);
    va_end(args);

    if (n < 0 || (size_t)n >= cap - *used) {
        return false;
    }

    *used += (size_t)n;
    return true;
}

/* A name from the board goes into JSON, thus the two characters that would
 * end the string early are dropped rather than escaped.
 */
static void copy_name(char *out, size_t cap, const uint8_t *src, size_t len) {
    size_t j = 0;

    for (size_t i = 0; i < len && j + 1 < cap; i++) {
        if (src[i] == '"' || src[i] == '\\' || src[i] < 0x20) {
            continue;
        }

        out[j++] = (char)src[i];
    }

    out[j] = '\0';
}

/****************************************************************************
 * The routes of the port
 ****************************************************************************/

static void route_devices(int fd) {
    char body[512];
    size_t used = 0;
    unsigned int at = 0;
    int rc = ask(IPC_OP_USB_LIST, NULL, 0);
    bool first = true;

    if (rc != 0 || g_replyOp != IPC_OP_USB_DEVS) {
        reply_result(fd, rc != 0 ? rc : -1);
        return;
    }

    if (!append(body, sizeof(body), &used, "{\"devices\":[")) {
        reply(fd, "500 Internal Server Error",
              "{\"ok\":false,\"error\":\"reply\"}\n");
        return;
    }

    while (at + IPC_USB_DEV_NAME <= g_replyLen) {
        const uint8_t *rec = &g_reply[at];
        unsigned int namelen = rec[IPC_USB_DEV_NAMELEN];
        char name[48];

        if (at + IPC_USB_DEV_NAME + namelen > g_replyLen) {
            break;
        }

        copy_name(name, sizeof(name), &rec[IPC_USB_DEV_NAME], namelen);
        if (!append(body, sizeof(body), &used,
                    "%s{\"channel\":%u,\"kind\":\"%s\",\"name\":\"%s\"}",
                    first ? "" : ",", rec[IPC_USB_DEV_CHANNEL],
                    rec[IPC_USB_DEV_KIND] == IPC_USB_KIND_STORAGE ? "storage"
                                                                  : "serial",
                    name)) {
            break;
        }

        first = false;
        at += IPC_USB_DEV_NAME + namelen;
    }

    if (!append(body, sizeof(body), &used, "]}\n")) {
        reply(fd, "500 Internal Server Error",
              "{\"ok\":false,\"error\":\"reply\"}\n");
        return;
    }

    reply_typed(fd, "200 OK", "application/json", body, used);
}

/* Bytes to a serial device. The sequence makes a repeat of one write leave
 * the stream unchanged, which no other opcode of this protocol needs.
 */

static void route_write(int fd, uint8_t channel, const char *body, size_t len) {
    struct channel_s *ch = &g_channels[channel];
    size_t room = sizeof(g_payload) - IPC_USB_WRITE_DATA;

    if (len > room) {
        len = room;
    }

    g_payload[IPC_USB_CHANNEL] = channel;
    g_payload[IPC_USB_WRITE_SEQ] = ch->seq;
    memcpy(&g_payload[IPC_USB_WRITE_DATA], body, len);

    if (ask(IPC_OP_USB_WRITE, g_payload,
            (uint16_t)(IPC_USB_WRITE_DATA + len)) == 0) {
        ch->seq++;
        reply(fd, "200 OK", "{\"ok\":true}\n");
        return;
    }

    /* The sequence stays put: a caller repeats the same write, and the board
     * answers the repeat with an ACK having written nothing twice.
     */

    reply_result(fd, -1);
}

/* What the device has said since the last read. */

static void route_read(int fd, uint8_t channel) {
    struct channel_s *ch = &g_channels[channel];
    char head[160];

    while (pump()) {
        ;
    }

    if (!ch->subscribed && ch->len == 0) {
        reply(fd, "409 Conflict",
              "{\"ok\":false,\"error\":\"the channel is not followed\"}\n");
        return;
    }

    /* The count of what was lost travels in a header: the body is the bytes
     * of the device and nothing else.
     */

    snprintf(head, sizeof(head),
             "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
             "X-Telegraph-Dropped: %u\r\n"
             "Content-Length: %u\r\nConnection: close\r\n\r\n",
             ch->dropped, ch->len);
    write(fd, head, strlen(head));
    write(fd, ch->buf, ch->len);
    ch->len = 0;
    ch->dropped = 0;
}

static void route_subscribe(int fd, uint8_t channel, bool on) {
    uint8_t payload[IPC_USB_SUB_LEN];
    int rc;

    payload[IPC_USB_CHANNEL] = channel;
    payload[IPC_USB_STATE] = on ? 1u : 0u;

    rc = ask(IPC_OP_USB_SUB, payload, sizeof(payload));
    if (rc == 0) {
        struct channel_s *ch = &g_channels[channel];

        ch->subscribed = on;
        /* The board forgets the sequence of a channel that opens or closes,
         * and what the buffer holds belongs to the session that ended.
         */
        ch->seq = 0;
        ch->len = 0;
        ch->dropped = 0;
    }

    reply_result(fd, rc);
}

/****************************************************************************
 * The routes of the storage
 ****************************************************************************/

/* Every path of a storage opcode starts at a root the board allows, thus a
 * request naming anything else is refused here rather than on the link.
 */

static bool path_ok(const char *path) {
    if (path == NULL || path[0] == '\0' || strlen(path) > IPC_FS_PATH_MAX) {
        return false;
    }

    if (strstr(path, "..") != NULL) {
        return false;
    }

    return strncmp(path, IPC_ROOT_MEDIA, strlen(IPC_ROOT_MEDIA)) == 0 ||
           strncmp(path, IPC_ROOT_ASSETS, strlen(IPC_ROOT_ASSETS)) == 0;
}

static void route_list(int fd, const char *path, unsigned int index) {
    char body[768];
    size_t used = 0;
    size_t plen = strlen(path);
    unsigned int at = 2;
    bool first = true;
    int rc;

    ipc_put_u16(&g_payload[IPC_FS_LIST_INDEX], (uint16_t)index);
    memcpy(&g_payload[IPC_FS_LIST_PATH], path, plen);

    rc = ask(IPC_OP_FS_LIST, g_payload, (uint16_t)(IPC_FS_LIST_PATH + plen));
    if (rc != 0 || g_replyOp != IPC_OP_FS_LIST || g_replyLen < 2) {
        reply_result(fd, rc != 0 ? rc : -1);
        return;
    }

    if (!append(body, sizeof(body), &used, "{\"next\":%u,\"entries\":[",
                ipc_get_u16(&g_reply[0]))) {
        reply(fd, "500 Internal Server Error",
              "{\"ok\":false,\"error\":\"reply\"}\n");
        return;
    }

    while (at + IPC_FS_ENTRY_NAME <= g_replyLen) {
        const uint8_t *rec = &g_reply[at];
        unsigned int namelen = rec[IPC_FS_ENTRY_NAMELEN];
        char name[64];

        if (at + IPC_FS_ENTRY_NAME + namelen > g_replyLen) {
            break;
        }

        copy_name(name, sizeof(name), &rec[IPC_FS_ENTRY_NAME], namelen);
        if (!append(body, sizeof(body), &used,
                    "%s{\"name\":\"%s\",\"kind\":\"%s\",\"size\":%u}",
                    first ? "" : ",", name,
                    rec[IPC_FS_ENTRY_KIND] == IPC_FS_KIND_DIR ? "dir" : "file",
                    (unsigned)ipc_get_u32(&rec[IPC_FS_ENTRY_SIZE]))) {
            break;
        }

        first = false;
        at += IPC_FS_ENTRY_NAME + namelen;
    }

    if (!append(body, sizeof(body), &used, "]}\n")) {
        reply(fd, "500 Internal Server Error",
              "{\"ok\":false,\"error\":\"reply\"}\n");
        return;
    }

    reply_typed(fd, "200 OK", "application/json", body, used);
}

/* One part of a file. A caller reads until it takes an empty body, which is
 * how the board states that the offset is at the end.
 */

static void route_file_read(int fd, const char *path, unsigned int offset,
                            unsigned int length) {
    size_t plen = strlen(path);
    char head[192];
    unsigned int got;
    int rc;

    if (length == 0 || length > IPC_FS_READ_MAX) {
        length = IPC_FS_READ_MAX;
    }

    ipc_put_u32(&g_payload[IPC_FS_READ_OFFSET], (uint32_t)offset);
    ipc_put_u16(&g_payload[IPC_FS_READ_LENGTH], (uint16_t)length);
    memcpy(&g_payload[IPC_FS_READ_PATH], path, plen);

    rc = ask(IPC_OP_FS_READ, g_payload, (uint16_t)(IPC_FS_READ_PATH + plen));
    if (rc != 0 || g_replyOp != IPC_OP_FS_READ ||
        g_replyLen < IPC_FS_READ_DATA) {
        reply_result(fd, rc != 0 ? rc : -1);
        return;
    }

    got = g_replyLen - IPC_FS_READ_DATA;
    snprintf(head, sizeof(head),
             "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
             "X-Telegraph-Offset: %u\r\n"
             "Content-Length: %u\r\nConnection: close\r\n\r\n",
             (unsigned)ipc_get_u32(&g_reply[IPC_FS_READ_OFFSET]), got);
    write(fd, head, strlen(head));
    write(fd, &g_reply[IPC_FS_READ_DATA], got);
}

/* One part of a file on the way in. A body that fits one frame carries both
 * marks, thus a small file needs one request.
 */

static void route_file_write(int fd, const char *path, const char *body,
                             size_t len, bool first, bool last) {
    size_t plen = strlen(path);
    size_t room = sizeof(g_payload) - IPC_FS_WRITE_PATH - plen;

    if (len > room) {
        reply(fd, "413 Payload Too Large",
              "{\"ok\":false,\"error\":\"one part holds less\"}\n");
        return;
    }

    g_payload[IPC_FS_WRITE_FLAGS] = (uint8_t)((first ? IPC_FS_WRITE_FIRST : 0) |
                                              (last ? IPC_FS_WRITE_LAST : 0));
    g_payload[IPC_FS_WRITE_PATHLEN] = (uint8_t)plen;
    memcpy(&g_payload[IPC_FS_WRITE_PATH], path, plen);
    memcpy(&g_payload[IPC_FS_WRITE_PATH + plen], body, len);

    reply_result(fd, ask(IPC_OP_FS_WRITE, g_payload,
                         (uint16_t)(IPC_FS_WRITE_PATH + plen + len)));
}

static void route_path_only(int fd, uint8_t opcode, const char *path) {
    reply_result(fd, ask(opcode, path, (uint16_t)strlen(path)));
}

/****************************************************************************
 * Routing
 ****************************************************************************/

/* The value of one key of a query, or the fallback. */

static unsigned int query_uint(const char *query, const char *key,
                               unsigned int fallback) {
    size_t klen = strlen(key);

    for (const char *p = query; p != NULL && *p != '\0';) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            return (unsigned int)strtoul(&p[klen + 1], NULL, 10);
        }

        p = strchr(p, '&');
        if (p != NULL) {
            p++;
        }
    }

    return fallback;
}

/* The text value of one key of a query, written into `out`. */

static bool query_str(const char *query, const char *key, char *out,
                      size_t cap) {
    size_t klen = strlen(key);

    for (const char *p = query; p != NULL && *p != '\0';) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *value = &p[klen + 1];
            const char *end = strchr(value, '&');
            size_t len = end != NULL ? (size_t)(end - value) : strlen(value);

            if (len >= cap) {
                return false;
            }

            memcpy(out, value, len);
            out[len] = '\0';
            return true;
        }

        p = strchr(p, '&');
        if (p != NULL) {
            p++;
        }
    }

    return false;
}

/* "/usb/<channel>/<verb>". Returns the verb, or NULL when the path is not of
 * that shape or the channel is not one the board holds.
 */

static const char *usb_path(const char *path, uint8_t *channel) {
    unsigned long value;
    char *end;

    if (strncmp(path, "/usb/", 5) != 0) {
        return NULL;
    }

    value = strtoul(&path[5], &end, 10);
    if (end == &path[5] || *end != '/' || value >= IPC_USB_CHANNELS) {
        return NULL;
    }

    *channel = (uint8_t)value;
    return end + 1;
}

static void route(int fd, char *target, const char *method, const char *body,
                  size_t blen) {
    bool post = strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0;
    bool get = strcmp(method, "GET") == 0;
    bool del = strcmp(method, "DELETE") == 0;
    char *query = strchr(target, '?');
    const char *path = target;
    char fspath[IPC_FS_PATH_MAX + 1];
    uint8_t channel;
    const char *verb;

    if (query != NULL) {
        *query++ = '\0';
    }

    if (get && strcmp(path, "/usb/devices") == 0) {
        route_devices(fd);
        return;
    }

    verb = usb_path(path, &channel);
    if (verb != NULL) {
        if (post && strcmp(verb, "write") == 0) {
            route_write(fd, channel, body, blen);
        } else if (get && strcmp(verb, "read") == 0) {
            route_read(fd, channel);
        } else if (post && strcmp(verb, "subscribe") == 0) {
            route_subscribe(fd, channel, true);
        } else if (del && strcmp(verb, "subscribe") == 0) {
            route_subscribe(fd, channel, false);
        } else {
            reply(fd, "404 Not Found",
                  "{\"ok\":false,\"error\":\"no route\"}\n");
        }

        return;
    }

    if (strncmp(path, "/storage", 8) != 0) {
        reply(fd, "404 Not Found", "{\"ok\":false,\"error\":\"no route\"}\n");
        return;
    }

    if (!query_str(query, "path", fspath, sizeof(fspath)) || !path_ok(fspath)) {
        reply(fd, "400 Bad Request",
              "{\"ok\":false,\"error\":\"path must sit under " IPC_ROOT_MEDIA
              " or " IPC_ROOT_ASSETS "\"}\n");
        return;
    }

    if (get && strcmp(path, "/storage") == 0) {
        route_list(fd, fspath, query_uint(query, "index", 0));
    } else if (get && strcmp(path, "/storage/file") == 0) {
        route_file_read(fd, fspath, query_uint(query, "offset", 0),
                        query_uint(query, "length", 0));
    } else if (post && strcmp(path, "/storage/file") == 0) {
        route_file_write(fd, fspath, body, blen,
                         query_uint(query, "first", 1) != 0,
                         query_uint(query, "last", 1) != 0);
    } else if (del && strcmp(path, "/storage/file") == 0) {
        route_path_only(fd, IPC_OP_FS_DELETE, fspath);
    } else if (post && strcmp(path, "/storage/dir") == 0) {
        route_path_only(fd, IPC_OP_FS_MKDIR, fspath);
    } else {
        reply(fd, "404 Not Found", "{\"ok\":false,\"error\":\"no route\"}\n");
    }
}

/* The value of Content-Length, or 0. The header names are not case-sensitive,
 * thus this walks the head itself.
 */

static long content_length(const char *head) {
    static const char key[] = "content-length:";

    for (const char *p = head; *p != '\0'; p++) {
        size_t i = 0;

        while (key[i] != '\0' && p[i] != '\0' && (p[i] | 0x20) == key[i]) {
            i++;
        }

        if (key[i] == '\0') {
            return strtol(&p[i], NULL, 10);
        }
    }

    return 0;
}

static void serve(int fd) {
    char method[8];
    char path[128];
    size_t len = 0;
    size_t hlen = 0;
    size_t blen = 0;
    const char *body = "";
    const char *sp1;
    const char *sp2;

    for (;;) {
        ssize_t n = read(fd, &g_request[len], REQUEST_MAX - len);

        if (n <= 0) {
            break;
        }

        len += (size_t)n;
        g_request[len] = '\0';

        const char *sep = strstr(g_request, "\r\n\r\n");
        if (sep == NULL) {
            if (len == REQUEST_MAX) {
                break;
            }

            continue;
        }

        hlen = (size_t)(sep - g_request) + 4;
        blen = len - hlen;
        if ((long)blen >= content_length(g_request) || len == REQUEST_MAX) {
            break;
        }
    }

    if (hlen == 0) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"request\"}\n");
        return;
    }

    /* The request line is "<method> <path> HTTP/1.1". */

    sp1 = memchr(g_request, ' ', hlen);
    sp2 = (sp1 != NULL)
              ? memchr(sp1 + 1, ' ', hlen - (size_t)(sp1 + 1 - g_request))
              : NULL;
    if (sp1 == NULL || sp2 == NULL ||
        (size_t)(sp1 - g_request) >= sizeof(method) ||
        (size_t)(sp2 - sp1) >= sizeof(path)) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"request\"}\n");
        return;
    }

    memcpy(method, g_request, (size_t)(sp1 - g_request));
    method[sp1 - g_request] = '\0';
    memcpy(path, sp1 + 1, (size_t)(sp2 - sp1 - 1));
    path[sp2 - sp1 - 1] = '\0';

    body = &g_request[hlen];
    g_request[hlen + blen] = '\0';

    route(fd, path, method, body, blen);
}

/****************************************************************************
 * The peer
 ****************************************************************************/

static int open_pipes(void) {
    char path[64];
    uint64_t deadline = now_ms() + REPLY_MS;

    snprintf(path, sizeof(path), TG_BRK_PIPE_RSP, g_name);
    while (g_rsp < 0 && now_ms() < deadline) {
        g_rsp = open(path, O_RDONLY | O_NONBLOCK);
        if (g_rsp < 0) {
            nap();
        }
    }

    snprintf(path, sizeof(path), TG_BRK_PIPE_REQ, g_name);
    while (g_req < 0 && now_ms() < deadline) {
        g_req = open(path, O_WRONLY);
        if (g_req < 0) {
            nap();
        }
    }

    return (g_req < 0 || g_rsp < 0) ? -1 : 0;
}

int main(void) {
    const char *name = getenv(PEER_ENV);
    const char *sock = getenv(SOCKET_ENV);
    char path[64];
    int lfd;

    if (name != NULL && name[0] != '\0') {
        snprintf(g_name, sizeof(g_name), "%s", name);
    }

    ipc_parser_init(&g_parser);

    if (open_pipes() < 0) {
        emit("usb: the pipes of the broker stayed closed\n");
        return 1;
    }

    snprintf(path, sizeof(path), "/net/%s",
             (sock != NULL && sock[0] != '\0') ? sock : SOCKET_DEFAULT);
    lfd = open(path, O_RDWR);
    if (lfd < 0) {
        emitf("usb: %s is out of reach\n", path);
        return 1;
    }

    emitf("usb: serving on %s for the peer %s\n", path, g_name);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);

        if (cfd < 0) {
            /* A push arrives whenever the device speaks, thus the buffer of a
             * channel fills between the requests of a client too.
             */
            while (pump()) {
                ;
            }

            nap();
            continue;
        }

        serve(cfd);
        close(cfd);
    }
}
