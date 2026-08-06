/* SPDX-License-Identifier: Apache-2.0 */

/* The broker of the link to the STM32.
 *
 * Note: the engine gives one UART to one wapp. This wapp holds that grant,
 * and every other wapp of the device reaches the board through its pipes.
 */

#include <errno.h>
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

/****************************************************************************
 * Definitions
 ****************************************************************************/

#define UART_DATA "/dev/uart/%s/data"
#define UART_BAUD "/dev/uart/%s/baud"
#define UART_FORMAT "/dev/uart/%s/format"

#define PORT_ENV "TELEGRAPH_UART_PORT"
#define PORT_DEFAULT "1"
#define PORT_MAX 8

/* The loop sleeps this long after a pass that did no work.
 *
 * Note: a sleep shorter than the tick of the system does not yield on every
 * platform. The value stays at or above the 10 ms tick of the edge MCU, or
 * the loop takes a whole processor and the watchdog of the system fires.
 */

#define IDLE_SLEEP_US 10000u

/* A request without a reply goes again after this time, and it fails after
 * this many attempts.
 */

#define REPLY_TIMEOUT_MS 200u
#define REPLY_TRIES 3u

/* The parser holds a partial frame no longer than this. */

#define PARSER_IDLE_MS 20u

/* The opcodes that one peer follows. */

#define PUSH_MAX 4u

#define READ_CHUNK 256u

/* A pipe holds 4096 bytes. A write that finds it full waits this many times
 * before the broker discards the frame.
 */

#define PIPE_RETRIES 200u
#define PIPE_RETRY_US 1000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct peer_s {
    char name[TG_BRK_NAME_MAX + 1];
    int req_fd;
    int rsp_fd;
    uint8_t pushes[PUSH_MAX];
    unsigned int npush;
    struct ipc_parser_s parser;
};

/* The request on the link. The broker keeps one request in flight, thus a
 * reply needs no queue and the credits of the link never run out.
 */

struct inflight_s {
    bool busy;
    uint16_t link_id;
    uint16_t peer_id;
    unsigned int peer;
    unsigned int tries;
    uint64_t sent_ms;
    uint16_t len;
    uint8_t frame[IPC_FRAME_MAX];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct peer_s g_peers[TG_BRK_MAX_PEERS];
static unsigned int g_npeers;

static struct ipc_parser_s g_link;
static struct inflight_s g_inflight;

static int g_uart = -1;
static char g_port[PORT_MAX] = PORT_DEFAULT;

/* The peer that holds raw mode, or -1. */

static int g_raw = -1;

static uint16_t g_next_id = 1;
static uint64_t g_link_rx_ms;
static uint8_t g_frame[IPC_FRAME_MAX];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void emit(const char *text) { write(STDOUT_FILENO, text, strlen(text)); }

static void emitf(const char *fmt, ...) {
    char line[128];
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

static void nap(unsigned int usec) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)usec * 1000};

    nanosleep(&ts, NULL);
}

/* Write a value to a node of the UART. */

static int write_node(const char *fmt, const char *value) {
    char path[64];
    int fd;
    int n;

    snprintf(path, sizeof(path), fmt, g_port);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        return -1;
    }

    n = (int)write(fd, value, strlen(value));
    close(fd);
    return n < 0 ? -1 : 0;
}

/* Write every byte, and give the count that reached the descriptor. */

static int write_all(int fd, const uint8_t *data, size_t len) {
    size_t done = 0;
    unsigned int retries = 0;

    while (done < len) {
        ssize_t n = write(fd, data + done, len - done);

        if (n > 0) {
            done += (size_t)n;
            retries = 0;
            continue;
        }

        if (n < 0 && errno != EAGAIN) {
            return -1;
        }

        if (++retries > PIPE_RETRIES) {
            return -1;
        }

        nap(PIPE_RETRY_US);
    }

    return 0;
}

/* Send one frame to a peer. */

static void to_peer(struct peer_s *peer, uint8_t opcode, uint16_t corr_id,
                    const void *payload, uint16_t len) {
    int n = ipc_encode(g_frame, sizeof(g_frame), opcode, corr_id, payload, len);

    if (n < 0 || peer->rsp_fd < 0) {
        return;
    }

    if (write_all(peer->rsp_fd, g_frame, (size_t)n) < 0) {
        emitf("broker: %s lost a frame, opcode 0x%02x\n", peer->name, opcode);
    }
}

static void nack_peer(struct peer_s *peer, uint16_t corr_id, uint8_t error) {
    to_peer(peer, IPC_OP_NACK, corr_id, &error, 1);
}

/****************************************************************************
 * The link
 ****************************************************************************/

/* Put the request that is in flight on the line. */

static void send_inflight(void) {
    if (write_all(g_uart, g_inflight.frame, g_inflight.len) < 0) {
        emit("broker: the line refused a frame\n");
    }

    g_inflight.sent_ms = now_ms();
    g_inflight.tries++;
}

/* Take a request from a peer and put it on the link. */

static void start_request(unsigned int peer, const struct ipc_frame_s *frame) {
    int n;

    g_inflight.link_id = g_next_id++;
    if (g_next_id == IPC_CORR_ID_PUSH) {
        g_next_id = 1;
    }

    n = ipc_encode(g_inflight.frame, sizeof(g_inflight.frame), frame->opcode,
                   g_inflight.link_id, frame->payload, frame->payload_len);
    if (n < 0) {
        nack_peer(&g_peers[peer], frame->corr_id, IPC_ERR_BAD_LENGTH);
        return;
    }

    g_inflight.busy = true;
    g_inflight.peer = peer;
    g_inflight.peer_id = frame->corr_id;
    g_inflight.len = (uint16_t)n;
    g_inflight.tries = 0;
    send_inflight();
}

/* Give a frame that no request asked for to every peer that follows it. */

static void demux_push(const struct ipc_frame_s *frame) {
    unsigned int i;
    unsigned int j;
    bool taken = false;

    for (i = 0; i < g_npeers; i++) {
        for (j = 0; j < g_peers[i].npush; j++) {
            if (g_peers[i].pushes[j] != frame->opcode) {
                continue;
            }

            to_peer(&g_peers[i], frame->opcode, IPC_CORR_ID_PUSH,
                    frame->payload, frame->payload_len);
            taken = true;
        }
    }

    if (!taken && frame->opcode == IPC_OP_LOG) {
        emitf("stm32: %.*s\n", (int)frame->payload_len, frame->payload);
    }
}

/* One frame from the STM32. */

static void on_link_frame(void *arg, const struct ipc_frame_s *frame) {
    (void)arg;

    if (frame->corr_id == IPC_CORR_ID_PUSH) {
        demux_push(frame);
        return;
    }

    if (!g_inflight.busy || frame->corr_id != g_inflight.link_id) {
        emitf("broker: a reply with the unknown ID 0x%04x\n", frame->corr_id);
        return;
    }

    to_peer(&g_peers[g_inflight.peer], frame->opcode, g_inflight.peer_id,
            frame->payload, frame->payload_len);
    g_inflight.busy = false;
}

/****************************************************************************
 * Raw mode
 ****************************************************************************/

/* Set the line to the settings of a raw payload, or back to the protocol. */

static int set_line(const struct ipc_frame_s *frame) {
    char baud[16];
    char format[8];

    if (frame->payload_len == 0) {
        snprintf(baud, sizeof(baud), "%u", (unsigned int)IPC_BAUD);
        snprintf(format, sizeof(format), "8N1");
    } else if (frame->payload_len == TG_BRK_RAW_LEN) {
        snprintf(baud, sizeof(baud), "%u",
                 (unsigned int)ipc_get_u32(&frame->payload[TG_BRK_RAW_BAUD]));
        snprintf(format, sizeof(format), "%c%c%c",
                 frame->payload[TG_BRK_RAW_DATABITS] + '0',
                 frame->payload[TG_BRK_RAW_PARITY],
                 frame->payload[TG_BRK_RAW_STOPBITS] + '0');
    } else {
        return -1;
    }

    if (write_node(UART_BAUD, baud) < 0 ||
        write_node(UART_FORMAT, format) < 0) {
        return -1;
    }

    emitf("broker: the line runs at %s %s\n", baud, format);
    return 0;
}

/* Take or leave raw mode. */

static void on_raw(unsigned int peer, const struct ipc_frame_s *frame) {
    bool leaving = frame->payload_len == 0;

    if (g_raw >= 0 && (unsigned int)g_raw != peer) {
        nack_peer(&g_peers[peer], frame->corr_id, IPC_ERR_BUSY);
        return;
    }

    if (!leaving && g_inflight.busy) {
        nack_peer(&g_peers[peer], frame->corr_id, IPC_ERR_BUSY);
        return;
    }

    if (set_line(frame) < 0) {
        nack_peer(&g_peers[peer], frame->corr_id, IPC_ERR_BAD_PAYLOAD);
        return;
    }

    /* The settings discard the receive buffer of the driver, thus the parser
     * of the link starts again as well.
     */

    ipc_parser_init(&g_link);
    g_raw = leaving ? -1 : (int)peer;
    emitf("broker: %s %s raw mode\n", g_peers[peer].name,
          leaving ? "left" : "holds");
    to_peer(&g_peers[peer], IPC_OP_ACK, frame->corr_id, NULL, 0);
}

/****************************************************************************
 * The peers
 ****************************************************************************/

/* One frame from a peer. */

static void on_peer_frame(void *arg, const struct ipc_frame_s *frame) {
    unsigned int peer = (unsigned int)(uintptr_t)arg;

    if (frame->opcode == TG_BRK_OP_RAW) {
        on_raw(peer, frame);
        return;
    }

    if (frame->opcode == TG_BRK_OP_RAW_DATA) {
        if (g_raw != (int)peer) {
            nack_peer(&g_peers[peer], frame->corr_id, IPC_ERR_BUSY);
            return;
        }

        write_all(g_uart, frame->payload, frame->payload_len);
        return;
    }

    if (g_raw >= 0 || g_inflight.busy) {
        nack_peer(&g_peers[peer], frame->corr_id, IPC_ERR_BUSY);
        return;
    }

    start_request(peer, frame);
}

/* Take one peer from an argument of the launch config.
 *
 * The form is <name>[:<opcode>[:<opcode>...]]. Each opcode names a frame that
 * no request asked for, and the peer receives every frame of that kind.
 */

static int add_peer(const char *spec) {
    struct peer_s *peer;
    const char *colon;
    size_t namelen;

    if (g_npeers >= TG_BRK_MAX_PEERS) {
        emitf("broker: %s does not fit, the broker holds %u peers\n", spec,
              (unsigned int)TG_BRK_MAX_PEERS);
        return -1;
    }

    peer = &g_peers[g_npeers];
    colon = strchr(spec, ':');
    namelen = colon != NULL ? (size_t)(colon - spec) : strlen(spec);
    if (namelen == 0 || namelen > TG_BRK_NAME_MAX) {
        emitf("broker: %s carries no usable name\n", spec);
        return -1;
    }

    memcpy(peer->name, spec, namelen);
    peer->name[namelen] = '\0';

    while (colon != NULL && peer->npush < PUSH_MAX) {
        peer->pushes[peer->npush++] = (uint8_t)strtoul(colon + 1, NULL, 0);
        colon = strchr(colon + 1, ':');
    }

    peer->req_fd = -1;
    peer->rsp_fd = -1;
    ipc_parser_init(&peer->parser);
    g_npeers++;
    return 0;
}

/* Open the pipes of a peer. A pipe that no peer opened yet gives an error,
 * thus the broker tries again at every pass.
 */

static void open_pipes(struct peer_s *peer) {
    char path[64];

    if (peer->req_fd < 0) {
        snprintf(path, sizeof(path), TG_BRK_PIPE_REQ, peer->name);
        peer->req_fd = open(path, O_RDONLY | O_NONBLOCK);
    }

    if (peer->rsp_fd < 0) {
        snprintf(path, sizeof(path), TG_BRK_PIPE_RSP, peer->name);
        peer->rsp_fd = open(path, O_WRONLY | O_NONBLOCK);
    }
}

/****************************************************************************
 * The loop
 ****************************************************************************/

/* Read the line, and give what it holds to the peer of raw mode or to the
 * parser of the link.
 */

static bool pump_link(void) {
    uint8_t buf[READ_CHUNK];
    ssize_t n = read(g_uart, buf, sizeof(buf));

    if (n <= 0) {
        return false;
    }

    g_link_rx_ms = now_ms();

    if (g_raw >= 0) {
        to_peer(&g_peers[g_raw], TG_BRK_OP_RAW_DATA, IPC_CORR_ID_PUSH, buf,
                (uint16_t)n);
        return true;
    }

    ipc_parser_push(&g_link, buf, (size_t)n, on_link_frame, NULL);
    return true;
}

/* Read the request pipe of every peer.
 *
 * Note: a request that waits for its reply stops this. The pipes then hold
 * what the peers write, thus the backpressure needs no queue of its own.
 */

static bool pump_peers(void) {
    uint8_t buf[READ_CHUNK];
    bool worked = false;
    unsigned int i;

    if (g_inflight.busy) {
        return false;
    }

    for (i = 0; i < g_npeers; i++) {
        ssize_t n;

        open_pipes(&g_peers[i]);
        if (g_peers[i].req_fd < 0) {
            continue;
        }

        n = read(g_peers[i].req_fd, buf, sizeof(buf));
        if (n <= 0) {
            continue;
        }

        ipc_parser_push(&g_peers[i].parser, buf, (size_t)n, on_peer_frame,
                        (void *)(uintptr_t)i);
        worked = true;
    }

    return worked;
}

/* Send a request again, and fail it after the last attempt.
 *
 * Note: the link loses about one frame in ten thousand, and such a loss
 * carries no error of its own. Thus a reply that never arrives is a retry and
 * not a fault of the peer.
 */

static void check_timeout(void) {
    if (!g_inflight.busy || now_ms() - g_inflight.sent_ms < REPLY_TIMEOUT_MS) {
        return;
    }

    if (g_inflight.tries >= REPLY_TRIES) {
        emitf("broker: no reply to the ID 0x%04x\n", g_inflight.link_id);
        nack_peer(&g_peers[g_inflight.peer], g_inflight.peer_id,
                  IPC_ERR_FAILED);
        g_inflight.busy = false;
        return;
    }

    send_inflight();
}

/* Corrupt data gives a false start of a frame, and the parser then waits for
 * bytes that no sender transmits. An idle line takes that state away.
 */

static void check_idle(void) {
    if (g_raw >= 0 || !ipc_parser_pending(&g_link)) {
        return;
    }

    if (now_ms() - g_link_rx_ms >= PARSER_IDLE_MS) {
        ipc_parser_timeout(&g_link, on_link_frame, NULL);
    }
}

int main(int argc, char **argv) {
    const char *port = getenv(PORT_ENV);
    char path[64];
    int i;

    if (port != NULL && port[0] != '\0') {
        snprintf(g_port, sizeof(g_port), "%s", port);
    }

    for (i = 1; i < argc; i++) {
        add_peer(argv[i]);
    }

    if (g_npeers == 0) {
        emit("broker: the launch config names no peer\n");
        return 1;
    }

    snprintf(path, sizeof(path), UART_DATA, g_port);
    g_uart = open(path, O_RDWR | O_NONBLOCK);
    if (g_uart < 0) {
        emitf("broker: %s is out of reach\n", path);
        return 1;
    }

    ipc_parser_init(&g_link);
    emitf("broker: the port %s serves %u peers\n", g_port, g_npeers);

    for (;;) {
        bool worked = pump_link();

        worked |= pump_peers();
        check_timeout();
        check_idle();

        if (!worked) {
            nap(IDLE_SLEEP_US);
        }
    }
}
