/* SPDX-License-Identifier: Apache-2.0 */

/* A peer of the broker that proves the two pipes carry a request, its reply
 * and a frame that no request asked for.
 *
 * Note: this wapp reaches no hardware. It exercises the broker alone, thus it
 * runs on a host build of the engine as well as on the device.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <telegraph/broker.h>

#define PEER_ENV "TELEGRAPH_PEER"
#define PEER_DEFAULT "probe"

/* Text for the main panel, and the whole round trip of the host test. */

#define TEXT_ENV "TELEGRAPH_TEXT"
#define FULL_ENV "TELEGRAPH_FULL"

/* The wapp gives up on the broker after this time. */

#define WAIT_MS 5000u

#define POLL_US 10000u

static char g_name[TG_BRK_NAME_MAX + 1] = PEER_DEFAULT;
static int g_req = -1;
static int g_rsp = -1;
static struct ipc_parser_s g_parser;
static uint8_t g_frame[IPC_FRAME_MAX];
static unsigned int g_replies;
static unsigned int g_pushes;

static void emit(const char *text) { write(STDOUT_FILENO, text, strlen(text)); }

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void nap(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = POLL_US * 1000};

    nanosleep(&ts, NULL);
}

/* Print one frame that the broker sent back. */

static void on_frame(void *arg, const struct ipc_frame_s *frame) {
    char line[128];

    (void)arg;

    snprintf(line, sizeof(line), "probe: opcode 0x%02x id 0x%04x len %u\n",
             frame->opcode, frame->corr_id, frame->payload_len);
    emit(line);

    /* The state of the board carries its version after the fixed bytes, thus
     * a reply from the real display is visible as such.
     */

    if (frame->opcode == IPC_OP_STATE && frame->payload_len > IPC_STATE_FWVER) {
        int n = (int)(frame->payload_len - IPC_STATE_FWVER);
        int temp = (int16_t)ipc_get_u16(&frame->payload[IPC_STATE_TEMP]);

        snprintf(line, sizeof(line),
                 "probe: the board runs %.*s at %d.%d degrees\n", n,
                 (const char *)&frame->payload[IPC_STATE_FWVER], temp / 10,
                 (temp < 0 ? -temp : temp) % 10);
        emit(line);
    }

    if (frame->corr_id == IPC_CORR_ID_PUSH) {
        g_pushes++;
    } else {
        g_replies++;
    }
}

static int open_pipes(void) {
    char path[64];
    uint64_t deadline = now_ms() + WAIT_MS;

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

/* Send one request, and give the frames of the broker to the parser until the
 * deadline passes.
 */

static void request(uint8_t opcode, uint16_t corr_id) {
    int n = ipc_encode(g_frame, sizeof(g_frame), opcode, corr_id, NULL, 0);
    uint64_t deadline = now_ms() + WAIT_MS;

    if (n < 0 || write(g_req, g_frame, (size_t)n) != n) {
        emit("probe: the request did not reach the broker\n");
        return;
    }

    while (now_ms() < deadline) {
        uint8_t buf[128];
        ssize_t got = read(g_rsp, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
            if (g_replies > 0) {
                return;
            }
            continue;
        }

        nap();
    }

    emit("probe: no reply\n");
}

/* Read the pipe of the broker until the count of the replies changes, or
 * until the deadline passes.
 */

static bool await_reply(unsigned int want) {
    uint64_t deadline = now_ms() + WAIT_MS;

    while (g_replies < want && now_ms() < deadline) {
        uint8_t buf[128];
        ssize_t got = read(g_rsp, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
            continue;
        }

        nap();
    }

    return g_replies >= want;
}

static void send(uint8_t opcode, uint16_t corr_id, const void *payload,
                 uint16_t len) {
    int n = ipc_encode(g_frame, sizeof(g_frame), opcode, corr_id, payload, len);

    if (n < 0 || write(g_req, g_frame, (size_t)n) != n) {
        emit("probe: the broker took no frame\n");
    }
}

/* Take raw mode, send bytes that carry no frame, and leave it again. The
 * flashing of the STM32 needs this path, thus the probe exercises it.
 */

static void raw_round(void) {
    uint8_t settings[TG_BRK_RAW_LEN];
    const char *bytes = "AN3155";
    unsigned int before = g_pushes;

    ipc_put_u32(&settings[TG_BRK_RAW_BAUD], 57600);
    settings[TG_BRK_RAW_DATABITS] = 8;
    settings[TG_BRK_RAW_PARITY] = 'E';
    settings[TG_BRK_RAW_STOPBITS] = 1;

    send(TG_BRK_OP_RAW, 0x2001, settings, sizeof(settings));
    if (!await_reply(2)) {
        emit("probe: raw mode stayed closed\n");
        return;
    }

    send(TG_BRK_OP_RAW_DATA, IPC_CORR_ID_PUSH, bytes, (uint16_t)strlen(bytes));

    uint64_t deadline = now_ms() + WAIT_MS;
    while (g_pushes == before && now_ms() < deadline) {
        uint8_t buf[128];
        ssize_t got = read(g_rsp, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
            continue;
        }

        nap();
    }

    if (g_pushes == before) {
        emit("probe: raw mode carried nothing back\n");
    }

    send(TG_BRK_OP_RAW, 0x2002, NULL, 0);
    await_reply(3);
}

/* Put text on the main panel, which the eye confirms. */

static void panel_text(const char *text) {
    uint8_t payload[64];
    size_t len = strlen(text);

    if (len > sizeof(payload) - IPC_TEXT_BODY) {
        len = sizeof(payload) - IPC_TEXT_BODY;
    }

    payload[IPC_TEXT_PANEL] = IPC_PANEL_MAIN;
    payload[IPC_TEXT_ATTRS] = IPC_ALIGN_CENTRE;
    memcpy(&payload[IPC_TEXT_BODY], text, len);

    send(IPC_OP_SET_TEXT, 0x1235, payload, (uint16_t)(IPC_TEXT_BODY + len));
    if (!await_reply(g_replies + 1)) {
        emit("probe: the panel took no text\n");
    }
}

/* Wait for a frame that no request asked for. */

static void wait_push(void) {
    uint64_t deadline = now_ms() + WAIT_MS;

    while (g_pushes == 0 && now_ms() < deadline) {
        uint8_t buf[128];
        ssize_t got = read(g_rsp, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
            continue;
        }

        nap();
    }
}

int main(void) {
    const char *name = getenv(PEER_ENV);
    char line[64];

    if (name != NULL && name[0] != '\0') {
        snprintf(g_name, sizeof(g_name), "%s", name);
    }

    ipc_parser_init(&g_parser);

    if (open_pipes() < 0) {
        emit("probe: the pipes of the broker stayed closed\n");
        return 1;
    }

    const char *text = getenv(TEXT_ENV);
    bool full = getenv(FULL_ENV) != NULL;

    request(IPC_OP_GET_STATE, 0x1234);

    if (text != NULL && text[0] != '\0') {
        panel_text(text);
    }

    /* The board sends a log frame at its own reset alone, and it echoes no
     * byte of raw mode. Thus these two run against the stub of the host test.
     */

    if (full) {
        wait_push();
        raw_round();
    }

    snprintf(line, sizeof(line), "probe: %u replies, %u pushes\n", g_replies,
             g_pushes);
    emit(line);
    return (g_replies > 0 && (!full || g_pushes > 0)) ? 0 : 1;
}
