/* SPDX-License-Identifier: Apache-2.0 */

/* The display over HTTP: the listening socket, and the routes of it.
 *
 * Note: an adapter. It translates HTTP into the request set of the display
 * and reaches neither the broker nor the STM32.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>

#include <telegraph/display.h>

/****************************************************************************
 * Definitions
 ****************************************************************************/

/* The name this wapp carries as a client of the display. */

#define CLIENT_ENV "TELEGRAPH_CLIENT"
#define CLIENT_DEFAULT "rest"

/* The name of the listening socket the launch config grants. */

#define SOCKET_ENV "TELEGRAPH_SOCKET"
#define SOCKET_DEFAULT "http"

#define REQUEST_MAX 1024u
#define BODY_MAX 512u
#define OPEN_MS 10000u
#define POLL_US 10000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct tg_dsp_client_s g_display;

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

static void nap(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = POLL_US * 1000};

    nanosleep(&ts, NULL);
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

/* What a refusal means. A text too long for a panel is the common one. */

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

/* The reply of a request that reached the display. */

static void reply_result(int fd, int rc) {
    if (rc == TG_DSP_OK) {
        reply(fd, "200 OK", "{\"ok\":true}\n");
    } else if (rc == TG_DSP_NACK) {
        char body[128];
        uint8_t code = g_display.reply_len > 0 ? g_display.reply[0] : 0;

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
    int rc = tg_dsp_ask(&g_display, TG_DSP_OP_GET_STATE, NULL, 0);
    const uint8_t *state = g_display.reply;
    unsigned int vlen;
    int temp;

    if (rc != TG_DSP_OK || g_display.reply_op != TG_DSP_OP_STATE ||
        g_display.reply_len < IPC_STATE_LEN) {
        reply_result(fd, rc != TG_DSP_OK ? rc : TG_DSP_NO_REPLY);
        return;
    }

    vlen = g_display.reply_len > IPC_STATE_FWVER
               ? g_display.reply_len - IPC_STATE_FWVER
               : 0;
    temp = (int16_t)ipc_get_u16(&state[IPC_STATE_TEMP]);

    snprintf(body, sizeof(body),
             "{\"time\":%u,\"temperature\":%d.%d,\"frames\":%u,"
             "\"crc_errors\":%u,\"resyncs\":%u,\"firmware\":\"%.*s\"}\n",
             (unsigned)ipc_get_u32(&state[IPC_STATE_TIME]), temp / 10,
             (temp < 0 ? -temp : temp) % 10,
             ipc_get_u16(&state[IPC_STATE_FRAMES]),
             ipc_get_u16(&state[IPC_STATE_CRC_ERR]), state[IPC_STATE_RESYNC],
             (int)vlen, (const char *)&state[IPC_STATE_FWVER]);
    reply(fd, "200 OK", body);
}

/* Text on a panel. An empty body clears that panel. */

static void route_text(int fd, uint8_t panel, const char *body, size_t len) {
    uint8_t payload[BODY_MAX + TG_DSP_TEXT_BODY];

    if (len > BODY_MAX) {
        len = BODY_MAX;
    }

    payload[TG_DSP_TEXT_PANEL] = panel;
    payload[TG_DSP_TEXT_ATTRS] = IPC_ALIGN_CENTRE;
    memcpy(&payload[TG_DSP_TEXT_BODY], body, len);
    reply_result(fd, tg_dsp_ask(&g_display, TG_DSP_OP_TEXT, payload,
                                (uint16_t)(TG_DSP_TEXT_BODY + len)));
}

/* A text that moves across a panel. */

static void route_scroll(int fd, uint8_t panel, const char *body, size_t len,
                         unsigned int period, unsigned int step) {
    uint8_t payload[BODY_MAX + TG_DSP_SCROLL_BODY];

    if (len > BODY_MAX) {
        len = BODY_MAX;
    }

    payload[TG_DSP_SCROLL_PANEL] = panel;
    ipc_put_u16(&payload[TG_DSP_SCROLL_PERIOD], (uint16_t)period);
    payload[TG_DSP_SCROLL_STEP] = (uint8_t)step;
    memcpy(&payload[TG_DSP_SCROLL_BODY], body, len);
    reply_result(fd, tg_dsp_ask(&g_display, TG_DSP_OP_SCROLL, payload,
                                (uint16_t)(TG_DSP_SCROLL_BODY + len)));
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

    levels[TG_DSP_BRIGHT_DIGITS] = (uint8_t)digits;
    levels[TG_DSP_BRIGHT_PANELS] = (uint8_t)(fields > 1 ? panels : digits);
    reply_result(
        fd, tg_dsp_ask(&g_display, TG_DSP_OP_BRIGHT, levels, sizeof(levels)));
}

/* The clock of the board. The RTC keeps UTC, and the offset is in minutes. */

static void route_clock(int fd, const char *body) {
    uint8_t payload[TG_DSP_TIME_TZ_LEN];
    unsigned long epoch = 0;
    long offset = 0;
    int fields = sscanf(body, "%lu %ld", &epoch, &offset);

    if (fields < 1 || epoch == 0) {
        epoch = (unsigned long)time(NULL);
    }

    ipc_put_u32(&payload[TG_DSP_TIME_EPOCH], (uint32_t)epoch);
    ipc_put_u16(&payload[TG_DSP_TIME_OFFSET], (uint16_t)(int16_t)offset);
    reply_result(
        fd, tg_dsp_ask(&g_display, TG_DSP_OP_TIME, payload, sizeof(payload)));
}

static void route_clear(int fd) {
    reply_result(fd, tg_dsp_ask(&g_display, TG_DSP_OP_CLEAR, NULL, 0));
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

    period = query_value(query, "period", TG_DSP_SCROLL_PERIOD_DEFAULT);
    step = query_value(query, "step", TG_DSP_SCROLL_STEP_DEFAULT);
    if (period == 0 || step == 0 || step > 70) {
        reply(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"query\"}\n");
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/state") == 0) {
        route_state(fd);
    } else if (put && strcmp(path, "/display/main") == 0) {
        route_text(fd, TG_DSP_PANEL_MAIN, body, blen);
    } else if (put && strcmp(path, "/display/sub") == 0) {
        route_text(fd, TG_DSP_PANEL_SUB, body, blen);
    } else if (put && strcmp(path, "/display/main/scroll") == 0) {
        route_scroll(fd, TG_DSP_PANEL_MAIN, body, blen, period, step);
    } else if (put && strcmp(path, "/display/sub/scroll") == 0) {
        route_scroll(fd, TG_DSP_PANEL_SUB, body, blen, period, step);
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

/* Content-Length, or 0. The header names are not case-sensitive. */

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
    const char *sp1;
    const char *sp2;

    for (;;) {
        ssize_t n = read(fd, &buf[len], REQUEST_MAX - len);

        if (n <= 0) {
            break;
        }

        len += (size_t)n;
        buf[len] = '\0';

        const char *sep = strstr(buf, "\r\n\r\n");
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
    /* cppcheck-suppress unreadVariable ; terminates `body`, which the
     * brightness and clock routes scan with sscanf */
    buf[hlen + blen] = '\0';

    route(fd, path, method, body, blen);
}

int main(void) {
    const char *name = getenv(CLIENT_ENV);
    const char *sock = getenv(SOCKET_ENV);
    char path[64];
    int lfd;

    if (name == NULL || name[0] == '\0') {
        name = CLIENT_DEFAULT;
    }

    if (tg_dsp_open(&g_display, name, OPEN_MS) < 0) {
        emitf("rest: the pipes of the display stayed closed for %s\n", name);
        return 1;
    }

    snprintf(path, sizeof(path), "/net/%s",
             (sock != NULL && sock[0] != '\0') ? sock : SOCKET_DEFAULT);
    lfd = open(path, O_RDWR);
    if (lfd < 0) {
        emitf("rest: %s is out of reach\n", path);
        return 1;
    }

    emitf("rest: serving on %s as the client %s\n", path, name);

    /* A stop of this wapp ends the wait inside accept with EINTR. Returning
     * on it leaves the wapp EXITED, and a wapp killed in its loop is FAILED.
     */

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);

        if (cfd < 0) {
            if (errno == EINTR) {
                emit("rest: stopped\n");
                return 0;
            }

            nap();
            continue;
        }

        serve(cfd);
        close(cfd);
    }
}
