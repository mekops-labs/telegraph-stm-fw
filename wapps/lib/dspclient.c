/* SPDX-License-Identifier: Apache-2.0 */

/* The client side of the display, compiled by every adapter. */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <telegraph/display.h>

/* The timeout of the display towards the board, with room for the pipe. */

#define REPLY_MS 4000u

/* A sleep below one tick of the scheduler busy-waits on the edge MCU, which
 * starves the idle task and resets the board. One tick is 10 ms.
 */

#define POLL_US 10000u

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void nap(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = POLL_US * 1000};

    nanosleep(&ts, NULL);
}

static void on_frame(void *arg, const struct ipc_frame_s *frame) {
    struct tg_dsp_client_s *client = arg;

    /* The display sends no unsolicited frame, thus this is the reply. */

    client->reply_op = frame->opcode;
    client->reply_len = frame->payload_len;
    if (client->reply_len > sizeof(client->reply)) {
        client->reply_len = (unsigned int)sizeof(client->reply);
    }

    memcpy(client->reply, frame->payload, client->reply_len);
    client->got_reply = true;
}

int tg_dsp_open(struct tg_dsp_client_s *client, const char *name,
                unsigned int timeout_ms) {
    char path[64];
    uint64_t deadline = now_ms() + timeout_ms;

    memset(client, 0, sizeof(*client));
    client->req_fd = -1;
    client->rsp_fd = -1;
    client->corr = 1;
    ipc_parser_init(&client->parser);

    /* The reply pipe first: a request sent before it opens has nowhere to
     * land.
     */

    snprintf(path, sizeof(path), TG_DSP_PIPE_RSP, name);
    while (client->rsp_fd < 0 && now_ms() < deadline) {
        client->rsp_fd = open(path, O_RDONLY | O_NONBLOCK);
        if (client->rsp_fd < 0) {
            nap();
        }
    }

    snprintf(path, sizeof(path), TG_DSP_PIPE_REQ, name);
    while (client->req_fd < 0 && now_ms() < deadline) {
        client->req_fd = open(path, O_WRONLY);
        if (client->req_fd < 0) {
            nap();
        }
    }

    return (client->req_fd < 0 || client->rsp_fd < 0) ? -1 : 0;
}

int tg_dsp_ask(struct tg_dsp_client_s *client, uint8_t opcode,
               const void *payload, uint16_t len) {
    int n = ipc_encode(client->frame, sizeof(client->frame), opcode,
                       client->corr++, payload, len);
    uint64_t deadline;

    if (client->corr == IPC_CORR_ID_PUSH) {
        client->corr = 1;
    }

    client->got_reply = false;
    if (n < 0 || write(client->req_fd, client->frame, (size_t)n) != n) {
        return TG_DSP_NO_REPLY;
    }

    deadline = now_ms() + REPLY_MS;
    while (!client->got_reply && now_ms() < deadline) {
        uint8_t buf[256];
        ssize_t got = read(client->rsp_fd, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&client->parser, buf, (size_t)got, on_frame,
                            client);
            continue;
        }

        nap();
    }

    if (!client->got_reply) {
        return TG_DSP_NO_REPLY;
    }

    return client->reply_op == IPC_OP_NACK ? TG_DSP_NACK : TG_DSP_OK;
}
