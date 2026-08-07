/* SPDX-License-Identifier: Apache-2.0 */

/* The display over HTTP.
 *
 * Note: a client reaches the panels, the digits and the clock of the STM32
 * through this wapp, and this wapp reaches them through the broker.
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
#define PEER_DEFAULT "display"

/* The name of the listening socket the launch config grants. */

#define SOCKET_ENV "TELEGRAPH_SOCKET"
#define SOCKET_DEFAULT "http"

#define REQUEST_MAX 1024u
#define BODY_MAX 512u
#define REPLY_MS 3000u
#define POLL_US 10000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_name[TG_BRK_NAME_MAX + 1] = PEER_DEFAULT;
static int g_req = -1;
static int g_rsp = -1;

static struct ipc_parser_s g_parser;
static uint8_t g_frame[IPC_FRAME_MAX];

static uint8_t g_reply[128];
static unsigned int g_replyLen;
static uint8_t g_replyOp;
static bool g_gotReply;

static uint16_t g_corr = 1;

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
 * The broker
 ****************************************************************************/

static void on_frame(void *arg, const struct ipc_frame_s *frame) {
    (void)arg;

    /* This wapp follows no opcode, thus every frame here answers a request. */

    g_replyOp = frame->opcode;
    g_replyLen = frame->payload_len;
    if (g_replyLen > sizeof(g_reply)) {
        g_replyLen = (unsigned int)sizeof(g_reply);
    }

    memcpy(g_reply, frame->payload, g_replyLen);
    g_gotReply = true;
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
        uint8_t buf[256];
        ssize_t got = read(g_rsp, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
            continue;
        }

        nap();
    }

    if (!g_gotReply) {
        return -1;
    }

    return g_replyOp == IPC_OP_NACK ? -2 : 0;
}

/****************************************************************************
 * HTTP
 ****************************************************************************/

static void reply(int fd, const char *status, const char *body) {
    char head[128];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\nContent-Type: application/json\r\n"
                     "Content-Length: %u\r\nConnection: close\r\n\r\n",
                     status, (unsigned)strlen(body));

    if (n > 0) {
        write(fd, head, (size_t)n);
    }

    write(fd, body, strlen(body));
}

/* What the board means by a NACK code. A text that does not fit the source of
 * a panel is the one a caller meets in practice.
 */

static const char *nack_name(uint8_t code) {
    switch (code) {
    case IPC_ERR_BAD_OPCODE:
        return "the board has no such operation";
    case IPC_ERR_BAD_LENGTH:
        return "too long for this panel";
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

/* The reply of a request that reached the board. */

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

/* The state of the board as JSON. */

static void route_state(int fd) {
    char body[256];
    int rc = ask(IPC_OP_GET_STATE, NULL, 0);

    if (rc != 0 || g_replyOp != IPC_OP_STATE || g_replyLen < IPC_STATE_LEN) {
        reply_result(fd, rc != 0 ? rc : -1);
        return;
    }

    unsigned int vlen =
        g_replyLen > IPC_STATE_FWVER ? g_replyLen - IPC_STATE_FWVER : 0;
    int temp = (int16_t)ipc_get_u16(&g_reply[IPC_STATE_TEMP]);

    snprintf(
        body, sizeof(body),
        "{\"time\":%u,\"temperature\":%d.%d,\"frames\":%u,"
        "\"crc_errors\":%u,\"resyncs\":%u,\"firmware\":\"%.*s\"}\n",
        (unsigned)ipc_get_u32(&g_reply[IPC_STATE_TIME]), temp / 10,
        (temp < 0 ? -temp : temp) % 10, ipc_get_u16(&g_reply[IPC_STATE_FRAMES]),
        ipc_get_u16(&g_reply[IPC_STATE_CRC_ERR]), g_reply[IPC_STATE_RESYNC],
        (int)vlen, (const char *)&g_reply[IPC_STATE_FWVER]);
    reply(fd, "200 OK", body);
}

/* Text on a panel. An empty body clears that panel. */

static void route_text(int fd, uint8_t panel, const char *body, size_t len) {
    uint8_t payload[BODY_MAX + IPC_TEXT_BODY];

    if (len > BODY_MAX) {
        len = BODY_MAX;
    }

    payload[IPC_TEXT_PANEL] = panel;
    payload[IPC_TEXT_ATTRS] = IPC_ALIGN_CENTRE;
    memcpy(&payload[IPC_TEXT_BODY], body, len);
    reply_result(
        fd, ask(IPC_OP_SET_TEXT, payload, (uint16_t)(IPC_TEXT_BODY + len)));
}

/* A text that moves across a panel, drawn by the board itself. */

static void route_scroll(int fd, uint8_t panel, const char *body, size_t len,
                         unsigned int width, unsigned int period,
                         unsigned int step) {
    uint8_t payload[BODY_MAX + IPC_ANIM_BODY];

    if (len > BODY_MAX) {
        len = BODY_MAX;
    }

    memset(payload, 0, IPC_ANIM_BODY);
    payload[IPC_ANIM_PANEL] = panel;
    payload[IPC_ANIM_W] = (uint8_t)width;
    payload[IPC_ANIM_H] = 14;
    payload[IPC_ANIM_FLAGS] = IPC_ANIM_TEXT;
    ipc_put_u16(&payload[IPC_ANIM_PERIOD], (uint16_t)period);
    payload[IPC_ANIM_STEP] = (uint8_t)step;
    memcpy(&payload[IPC_ANIM_BODY], body, len);
    reply_result(
        fd, ask(IPC_OP_SET_ANIM, payload, (uint16_t)(IPC_ANIM_BODY + len)));
}

/* The brightness of the digits and of the panels. */

static void route_brightness(int fd, const char *body) {
    uint8_t levels[2];
    unsigned int digits = 0;
    unsigned int panels = 0;
    int fields = sscanf(body, "%u %u", &digits, &panels);

    if (fields < 1 || digits > IPC_BRIGHT_MAX || panels > IPC_BRIGHT_MAX) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"level\"}\n");
        return;
    }

    levels[0] = (uint8_t)digits;
    levels[1] = (uint8_t)(fields > 1 ? panels : digits);
    reply_result(fd, ask(IPC_OP_SET_BRIGHT, levels, sizeof(levels)));
}

/* The clock of the board. The RTC keeps UTC, and the offset is in minutes. */

static void route_clock(int fd, const char *body) {
    uint8_t payload[IPC_SET_TIME_TZ_LEN];
    unsigned long epoch = 0;
    long offset = 0;
    int fields = sscanf(body, "%lu %ld", &epoch, &offset);

    if (fields < 1 || epoch == 0) {
        epoch = (unsigned long)time(NULL);
    }

    ipc_put_u32(&payload[0], (uint32_t)epoch);
    ipc_put_u16(&payload[IPC_SET_TIME_LEN], (uint16_t)(int16_t)offset);
    reply_result(fd, ask(IPC_OP_SET_TIME, payload, sizeof(payload)));
}

static void route_clear(int fd) {
    reply_result(fd, ask(IPC_OP_CLEAR, NULL, 0));
}

/* The value of one key of a query, or the fallback. */

static unsigned int query_value(const char *query, const char *key,
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

/* One request, already read whole. */

static void route(int fd, char *target, const char *method, const char *body,
                  size_t blen) {
    bool put = strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0;
    char *query = strchr(target, '?');
    const char *path = target;
    unsigned int period;
    unsigned int step;

    if (query != NULL) {
        *query++ = '\0';
    }

    period = query_value(query, "period", 60);
    step = query_value(query, "step", 1);
    if (period == 0 || step == 0 || step > 70) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"query\"}\n");
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/state") == 0) {
        route_state(fd);
    } else if (put && strcmp(path, "/display/main") == 0) {
        route_text(fd, IPC_PANEL_MAIN, body, blen);
    } else if (put && strcmp(path, "/display/sub") == 0) {
        route_text(fd, IPC_PANEL_SUB, body, blen);
    } else if (put && strcmp(path, "/display/main/scroll") == 0) {
        route_scroll(fd, IPC_PANEL_MAIN, body, blen, 70, period, step);
    } else if (put && strcmp(path, "/display/sub/scroll") == 0) {
        route_scroll(fd, IPC_PANEL_SUB, body, blen, 21, period, step);
    } else if (put && strcmp(path, "/brightness") == 0) {
        route_brightness(fd, body);
    } else if (put && strcmp(path, "/clock") == 0) {
        route_clock(fd, body);
    } else if (strcmp(method, "DELETE") == 0 && strcmp(path, "/display") == 0) {
        route_clear(fd);
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

/* Read a whole request: the head, then as much body as its length names. */

static void serve(int fd) {
    char buf[REQUEST_MAX + 1];
    char method[8];
    char path[64];
    size_t len = 0;
    size_t hlen = 0;
    size_t blen = 0;
    const char *body = "";
    const char *sep;
    const char *sp1;
    const char *sp2;

    for (;;) {
        ssize_t n = read(fd, &buf[len], REQUEST_MAX - len);

        if (n <= 0) {
            break;
        }

        len += (size_t)n;
        buf[len] = '\0';

        sep = strstr(buf, "\r\n\r\n");
        if (sep == NULL) {
            if (len == REQUEST_MAX) {
                break;
            }

            continue;
        }

        hlen = (size_t)(sep - buf) + 4;
        blen = len - hlen;
        if ((long)blen >= content_length(buf) || len == REQUEST_MAX) {
            break;
        }
    }

    if (hlen == 0) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"request\"}\n");
        return;
    }

    /* The request line is "<method> <path> HTTP/1.1". */

    sp1 = memchr(buf, ' ', hlen);
    sp2 = (sp1 != NULL) ? memchr(sp1 + 1, ' ', hlen - (size_t)(sp1 + 1 - buf))
                        : NULL;
    if (sp1 == NULL || sp2 == NULL || (size_t)(sp1 - buf) >= sizeof(method) ||
        (size_t)(sp2 - sp1) >= sizeof(path)) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"request\"}\n");
        return;
    }

    memcpy(method, buf, (size_t)(sp1 - buf));
    method[sp1 - buf] = '\0';
    memcpy(path, sp1 + 1, (size_t)(sp2 - sp1 - 1));
    path[sp2 - sp1 - 1] = '\0';

    body = &buf[hlen];
    buf[hlen + blen] = '\0';

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
        emit("display: the pipes of the broker stayed closed\n");
        return 1;
    }

    snprintf(path, sizeof(path), "/net/%s",
             (sock != NULL && sock[0] != '\0') ? sock : SOCKET_DEFAULT);
    lfd = open(path, O_RDWR);
    if (lfd < 0) {
        emitf("display: %s is out of reach\n", path);
        return 1;
    }

    emitf("display: serving on %s for the peer %s\n", path, g_name);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);

        if (cfd < 0) {
            nap();
            continue;
        }

        serve(cfd);
        close(cfd);
    }
}
