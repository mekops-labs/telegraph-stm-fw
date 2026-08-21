/* SPDX-License-Identifier: Apache-2.0 */

/* The display of the board: the composition, the digits and the clock.
 *
 * Note: it reaches the STM32 through the broker, and serves its clients the
 * request set of telegraph/display.h over a pipe pair each.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <telegraph/broker.h>
#include <telegraph/display.h>

/****************************************************************************
 * Definitions
 ****************************************************************************/

/* The name this wapp carries as a peer of the broker. */

#define PEER_ENV "TELEGRAPH_PEER"
#define PEER_DEFAULT "display"

#define BODY_MAX 512u
#define REPLY_MS 3000u
#define POLL_US 10000u
#define IDLE_SLEEP_US 5000u

/* A full pipe waits this many times before the display drops the reply. */

#define PIPE_RETRIES 200u
#define PIPE_RETRY_US 1000u

/* The panels in pixels. A movement is as wide as its panel. */

#define PANEL_MAIN_W 70u
#define PANEL_SUB_W 21u
#define PANEL_H 14u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct client_s {
    char name[TG_DSP_NAME_MAX + 1];
    int req_fd;
    int rsp_fd;
    struct ipc_parser_s parser;
};

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

static struct client_s g_clients[TG_DSP_MAX_CLIENTS];
static unsigned int g_nclients;

/* What a launch config that names no client gets. */

static const char *const g_defaultClients[] = {
    "rest",
    "hass",
};

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

static void nap_us(unsigned int us) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)us * 1000};

    nanosleep(&ts, NULL);
}

static void nap(void) { nap_us(POLL_US); }

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
 * The clients
 ****************************************************************************/

/* One frame to a client. A pipe that stays full costs the reply. */

static void to_client(struct client_s *client, uint8_t opcode, uint16_t corr,
                      const void *payload, uint16_t len) {
    uint8_t frame[IPC_FRAME_MAX];
    int n = ipc_encode(frame, sizeof(frame), opcode, corr, payload, len);
    unsigned int tries;

    if (n < 0 || client->rsp_fd < 0) {
        return;
    }

    for (tries = 0; tries < PIPE_RETRIES; tries++) {
        if (write(client->rsp_fd, frame, (size_t)n) == n) {
            return;
        }

        nap_us(PIPE_RETRY_US);
    }

    emitf("display: %s took no reply\n", client->name);
}

static void ack_client(struct client_s *client, uint16_t corr) {
    to_client(client, IPC_OP_ACK, corr, NULL, 0);
}

static void nack_client(struct client_s *client, uint16_t corr, uint8_t code) {
    to_client(client, IPC_OP_NACK, corr, &code, 1);
}

/* A board that gave no reply is IPC_ERR_BUSY. */

static void result_to_client(struct client_s *client, uint16_t corr, int rc) {
    if (rc == 0) {
        ack_client(client, corr);
    } else if (rc == -2) {
        nack_client(client, corr, g_replyLen > 0 ? g_reply[0] : IPC_ERR_FAILED);
    } else {
        nack_client(client, corr, IPC_ERR_BUSY);
    }
}

static bool panel_ok(uint8_t panel) {
    return panel == TG_DSP_PANEL_MAIN || panel == TG_DSP_PANEL_SUB;
}

/****************************************************************************
 * The requests
 ****************************************************************************/

/* The state of the board, forwarded as the board gave it. */

static void do_state(struct client_s *client, uint16_t corr) {
    int rc = ask(IPC_OP_GET_STATE, NULL, 0);

    if (rc != 0) {
        result_to_client(client, corr, rc);
        return;
    }

    if (g_replyOp != IPC_OP_STATE || g_replyLen < IPC_STATE_LEN) {
        nack_client(client, corr, IPC_ERR_FAILED);
        return;
    }

    to_client(client, TG_DSP_OP_STATE, corr, g_reply, (uint16_t)g_replyLen);
}

/* Text on a panel: the request has the layout the board takes. */

static void do_text(struct client_s *client, uint16_t corr,
                    const uint8_t *payload, uint16_t len) {
    uint8_t frame[BODY_MAX + IPC_TEXT_BODY];
    uint16_t body;

    if (len < TG_DSP_TEXT_BODY) {
        nack_client(client, corr, IPC_ERR_BAD_LENGTH);
        return;
    }

    body = (uint16_t)(len - TG_DSP_TEXT_BODY);

    if (!panel_ok(payload[TG_DSP_TEXT_PANEL]) ||
        (payload[TG_DSP_TEXT_ATTRS] & ~IPC_TEXT_ATTR_MASK) != 0) {
        nack_client(client, corr, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    if (body > BODY_MAX) {
        body = BODY_MAX;
    }

    frame[IPC_TEXT_PANEL] = payload[TG_DSP_TEXT_PANEL];
    frame[IPC_TEXT_ATTRS] = payload[TG_DSP_TEXT_ATTRS];
    memcpy(&frame[IPC_TEXT_BODY], &payload[TG_DSP_TEXT_BODY], body);
    result_to_client(
        client, corr,
        ask(IPC_OP_SET_TEXT, frame, (uint16_t)(IPC_TEXT_BODY + body)));
}

/* A text that moves, drawn by the board. The width belongs to this wapp. */

static void do_scroll(struct client_s *client, uint16_t corr,
                      const uint8_t *payload, uint16_t len) {
    uint8_t frame[BODY_MAX + IPC_ANIM_BODY];
    unsigned int width;
    unsigned int period;
    unsigned int step;
    uint16_t body;

    if (len < TG_DSP_SCROLL_BODY) {
        nack_client(client, corr, IPC_ERR_BAD_LENGTH);
        return;
    }

    body = (uint16_t)(len - TG_DSP_SCROLL_BODY);

    if (!panel_ok(payload[TG_DSP_SCROLL_PANEL])) {
        nack_client(client, corr, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    width = payload[TG_DSP_SCROLL_PANEL] == TG_DSP_PANEL_MAIN ? PANEL_MAIN_W
                                                              : PANEL_SUB_W;
    period = ipc_get_u16(&payload[TG_DSP_SCROLL_PERIOD]);
    step = payload[TG_DSP_SCROLL_STEP];
    if (period == 0) {
        period = TG_DSP_SCROLL_PERIOD_DEFAULT;
    }

    if (step == 0) {
        step = TG_DSP_SCROLL_STEP_DEFAULT;
    }

    if (step > width) {
        step = width;
    }

    if (body > BODY_MAX) {
        body = BODY_MAX;
    }

    memset(frame, 0, IPC_ANIM_BODY);
    frame[IPC_ANIM_PANEL] = payload[TG_DSP_SCROLL_PANEL];
    frame[IPC_ANIM_W] = (uint8_t)width;
    frame[IPC_ANIM_H] = PANEL_H;
    frame[IPC_ANIM_FLAGS] = IPC_ANIM_TEXT;
    ipc_put_u16(&frame[IPC_ANIM_PERIOD], (uint16_t)period);
    frame[IPC_ANIM_STEP] = (uint8_t)step;
    memcpy(&frame[IPC_ANIM_BODY], &payload[TG_DSP_SCROLL_BODY], body);
    result_to_client(
        client, corr,
        ask(IPC_OP_SET_ANIM, frame, (uint16_t)(IPC_ANIM_BODY + body)));
}

/* The brightness of the digits and of the panels. One byte sets both. */

static void do_bright(struct client_s *client, uint16_t corr,
                      const uint8_t *payload, uint16_t len) {
    uint8_t levels[2];

    if (len < TG_DSP_BRIGHT_LEN) {
        nack_client(client, corr, IPC_ERR_BAD_LENGTH);
        return;
    }

    levels[0] = payload[TG_DSP_BRIGHT_DIGITS];
    levels[1] = len >= TG_DSP_BRIGHT2_LEN ? payload[TG_DSP_BRIGHT_PANELS]
                                          : payload[TG_DSP_BRIGHT_DIGITS];
    if (levels[0] > IPC_BRIGHT_MAX || levels[1] > IPC_BRIGHT_MAX) {
        nack_client(client, corr, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    result_to_client(client, corr,
                     ask(IPC_OP_SET_BRIGHT, levels, sizeof(levels)));
}

/* The clock of the board. The RTC keeps UTC, and the offset is in minutes. */

static void do_time(struct client_s *client, uint16_t corr,
                    const uint8_t *payload, uint16_t len) {
    uint8_t frame[IPC_SET_TIME_TZ_LEN];
    uint32_t epoch;
    uint16_t offset = 0;

    if (len < TG_DSP_TIME_LEN) {
        nack_client(client, corr, IPC_ERR_BAD_LENGTH);
        return;
    }

    epoch = ipc_get_u32(&payload[TG_DSP_TIME_EPOCH]);
    if (epoch == 0) {
        epoch = (uint32_t)time(NULL);
    }

    if (len >= TG_DSP_TIME_TZ_LEN) {
        offset = ipc_get_u16(&payload[TG_DSP_TIME_OFFSET]);
    }

    ipc_put_u32(&frame[0], epoch);
    ipc_put_u16(&frame[IPC_SET_TIME_LEN], offset);
    result_to_client(client, corr, ask(IPC_OP_SET_TIME, frame, sizeof(frame)));
}

static void do_clear(struct client_s *client, uint16_t corr) {
    result_to_client(client, corr, ask(IPC_OP_CLEAR, NULL, 0));
}

static void on_client_frame(void *arg, const struct ipc_frame_s *frame) {
    struct client_s *client = arg;

    switch (frame->opcode) {
    case TG_DSP_OP_GET_STATE:
        do_state(client, frame->corr_id);
        break;

    case TG_DSP_OP_TEXT:
        do_text(client, frame->corr_id, frame->payload, frame->payload_len);
        break;

    case TG_DSP_OP_SCROLL:
        do_scroll(client, frame->corr_id, frame->payload, frame->payload_len);
        break;

    case TG_DSP_OP_BRIGHT:
        do_bright(client, frame->corr_id, frame->payload, frame->payload_len);
        break;

    case TG_DSP_OP_TIME:
        do_time(client, frame->corr_id, frame->payload, frame->payload_len);
        break;

    case TG_DSP_OP_CLEAR:
        do_clear(client, frame->corr_id);
        break;

    default:
        nack_client(client, frame->corr_id, IPC_ERR_BAD_OPCODE);
        break;
    }
}

/* Take one client from an argument of the launch config. */

static int add_client(const char *name) {
    struct client_s *client;
    size_t len = strlen(name);

    if (g_nclients >= TG_DSP_MAX_CLIENTS) {
        emitf("display: %s does not fit, the display holds %u clients\n", name,
              (unsigned int)TG_DSP_MAX_CLIENTS);
        return -1;
    }

    if (len == 0 || len > TG_DSP_NAME_MAX) {
        emitf("display: %s carries no usable name\n", name);
        return -1;
    }

    client = &g_clients[g_nclients];
    memcpy(client->name, name, len + 1);
    client->req_fd = -1;
    client->rsp_fd = -1;
    ipc_parser_init(&client->parser);
    g_nclients++;
    return 0;
}

/* A pipe no client opened yet fails, thus this runs at every pass. */

static void open_client_pipes(struct client_s *client) {
    char path[64];

    if (client->req_fd < 0) {
        snprintf(path, sizeof(path), TG_DSP_PIPE_REQ, client->name);
        client->req_fd = open(path, O_RDONLY | O_NONBLOCK);
    }

    if (client->rsp_fd < 0) {
        snprintf(path, sizeof(path), TG_DSP_PIPE_RSP, client->name);
        client->rsp_fd = open(path, O_WRONLY | O_NONBLOCK);
    }
}

/* One request runs to its reply before the next client is read, thus the
 * requests behind it wait in the pipes of their own clients.
 */

static bool pump_clients(void) {
    bool worked = false;
    unsigned int i;

    for (i = 0; i < g_nclients; i++) {
        uint8_t buf[256];
        ssize_t n;

        open_client_pipes(&g_clients[i]);
        if (g_clients[i].req_fd < 0) {
            continue;
        }

        n = read(g_clients[i].req_fd, buf, sizeof(buf));
        if (n <= 0) {
            continue;
        }

        worked = true;
        ipc_parser_push(&g_clients[i].parser, buf, (size_t)n, on_client_frame,
                        &g_clients[i]);
    }

    return worked;
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

int main(int argc, char **argv) {
    const char *name = getenv(PEER_ENV);
    int i;

    if (name != NULL && name[0] != '\0') {
        snprintf(g_name, sizeof(g_name), "%s", name);
    }

    for (i = 1; i < argc; i++) {
        add_client(argv[i]);
    }

    if (g_nclients == 0) {
        size_t d;

        for (d = 0; d < sizeof(g_defaultClients) / sizeof(g_defaultClients[0]);
             d++) {
            add_client(g_defaultClients[d]);
        }
    }

    ipc_parser_init(&g_parser);

    if (open_pipes() < 0) {
        emit("display: the pipes of the broker stayed closed\n");
        return 1;
    }

    emitf("display: the peer %s serves %u clients\n", g_name, g_nclients);

    for (;;) {
        if (!pump_clients()) {
            nap_us(IDLE_SLEEP_US);
        }
    }
}
