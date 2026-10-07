/* SPDX-License-Identifier: Apache-2.0 */

/* The wapp tg-time keeps the clock of the engine and the DS3231 of the STM32
 * together. The decisions are in telegraph/timepolicy.h, and the behaviour is
 * in docs/time.md.
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
#include <telegraph/timepolicy.h>

/* The launch config carries these. Each has a default that works on the
 * device, and the test of the host shortens them.
 */

#define PEER_ENV "TELEGRAPH_PEER"
#define PEER_DEFAULT "time"
#define INTERVAL_ENV "TELEGRAPH_TIME_INTERVAL_S"
#define INTERVAL_DEFAULT_S 30u
#define LOOPS_ENV "TELEGRAPH_TIME_LOOPS"

/* The clock device of the engine, and the config mount. */

#define RTC_SOURCE "/dev/rtc/main/source"
#define RTC_STATUS "/dev/rtc/main/status"
#define RTC_TIME "/dev/rtc/main/time"
#define CONFIG_PATH "/etc/tg-time.conf"

/* The wapp gives up on one reply of the broker after this time. */

#define WAIT_MS 5000u
#define POLL_US 10000u

#define CORR_FIRST 0x7100u

static char g_name[TG_BRK_NAME_MAX + 1] = PEER_DEFAULT;
static int g_req = -1;
static int g_rsp = -1;
static struct ipc_parser_s g_parser;
static uint8_t g_frame[IPC_FRAME_MAX];

/* The reply to the request that waits: the frame the broker sent back. */

static uint16_t g_corr = CORR_FIRST;
static bool g_have_reply;
static uint8_t g_reply_op;
static uint8_t g_reply[IPC_MAX_PAYLOAD];
static uint16_t g_reply_len;

static int g_offset_min;
static bool g_offset_pending = true;
static bool g_have_frames;
static uint16_t g_frames;

static void say(const char *fmt, ...) {
    char line[96];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (n > 0) {
        write(STDOUT_FILENO, line, (size_t)n);
    }
}

static void note(const char *text) { write(STDOUT_FILENO, text, strlen(text)); }

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void nap_us(unsigned int us) {
    struct timespec ts = {.tv_sec = us / 1000000u,
                          .tv_nsec = (long)(us % 1000000u) * 1000};

    nanosleep(&ts, NULL);
}

/****************************************************************************
 * The broker
 ****************************************************************************/

static void on_frame(void *arg, const struct ipc_frame_s *frame) {
    (void)arg;

    if (frame->corr_id != g_corr || g_have_reply) {
        return;
    }

    if (frame->opcode != IPC_OP_STATE && frame->opcode != IPC_OP_ACK &&
        frame->opcode != IPC_OP_NACK) {
        return;
    }

    g_reply_op = frame->opcode;
    g_reply_len = frame->payload_len < sizeof(g_reply)
                      ? frame->payload_len
                      : (uint16_t)sizeof(g_reply);
    memcpy(g_reply, frame->payload, g_reply_len);
    g_have_reply = true;
}

static int open_pipes(void) {
    char path[64];
    uint64_t deadline = now_ms() + WAIT_MS;

    snprintf(path, sizeof(path), TG_BRK_PIPE_RSP, g_name);
    while (g_rsp < 0 && now_ms() < deadline) {
        g_rsp = open(path, O_RDONLY | O_NONBLOCK);
        if (g_rsp < 0) {
            nap_us(POLL_US);
        }
    }

    snprintf(path, sizeof(path), TG_BRK_PIPE_REQ, g_name);
    while (g_req < 0 && now_ms() < deadline) {
        g_req = open(path, O_WRONLY);
        if (g_req < 0) {
            nap_us(POLL_US);
        }
    }

    return (g_req < 0 || g_rsp < 0) ? -1 : 0;
}

/* Send one request and wait for its reply. Returns the opcode of the reply,
 * or -1 when none came.
 */

static int ask(uint8_t opcode, const void *payload, uint16_t len) {
    uint64_t deadline = now_ms() + WAIT_MS;
    int n;

    g_corr = (uint16_t)(g_corr == 0xffffu ? CORR_FIRST : g_corr + 1u);
    g_have_reply = false;

    n = ipc_encode(g_frame, sizeof(g_frame), opcode, g_corr, payload, len);
    if (n < 0 || write(g_req, g_frame, (size_t)n) != n) {
        return -1;
    }

    while (!g_have_reply && now_ms() < deadline) {
        uint8_t buf[128];
        ssize_t got = read(g_rsp, buf, sizeof(buf));

        if (got > 0) {
            ipc_parser_push(&g_parser, buf, (size_t)got, on_frame, NULL);
            continue;
        }

        nap_us(POLL_US);
    }

    return g_have_reply ? g_reply_op : -1;
}

/* The time of the STM32 and the count of the frames it accepted. */

static bool read_stm(uint32_t *seconds, uint16_t *frames) {
    if (ask(IPC_OP_GET_STATE, NULL, 0) != IPC_OP_STATE ||
        g_reply_len < IPC_STATE_LEN) {
        return false;
    }

    *seconds = ipc_get_u32(&g_reply[IPC_STATE_TIME]);
    *frames = ipc_get_u16(&g_reply[IPC_STATE_FRAMES]);
    return true;
}

/* Send the time and the offset. The STM32 writes the DS3231 and keeps the
 * offset in its flash, and a write that changes nothing costs no erase.
 */

static bool push_stm(uint32_t seconds) {
    uint8_t payload[IPC_SET_TIME_TZ_LEN];

    ipc_put_u32(&payload[IPC_SET_TIME_UTC], seconds);
    ipc_put_u16(&payload[IPC_SET_TIME_OFFSET], (uint16_t)(int16_t)g_offset_min);

    if (ask(IPC_OP_SET_TIME, payload, sizeof(payload)) != IPC_OP_ACK) {
        note("tg-time: the STM32 took no time\n");
        return false;
    }

    say("tg-time: sent %lu to the STM32, offset %d min\n",
        (unsigned long)seconds, g_offset_min);
    return true;
}

/****************************************************************************
 * The clock of the engine
 ****************************************************************************/

/* Read a text node. Returns the length, or -1. */

static int read_node(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }

    n = read(fd, buf, size - 1u);
    close(fd);
    if (n < 0) {
        return -1;
    }

    buf[n] = '\0';
    return (int)n;
}

static bool write_engine(uint32_t seconds) {
    char line[24];
    int n = snprintf(line, sizeof(line), "%lu rtc", (unsigned long)seconds);
    int fd = open(RTC_TIME, O_WRONLY);
    bool ok;

    if (fd < 0) {
        note("tg-time: the engine clock takes no write\n");
        return false;
    }

    ok = write(fd, line, (size_t)n) == n;
    close(fd);
    if (ok) {
        say("tg-time: set the engine clock to %lu from the STM32\n",
            (unsigned long)seconds);
    } else {
        note("tg-time: the engine refused the time\n");
    }

    return ok;
}

static void read_engine(struct tg_time_in *in) {
    char buf[32];
    int n = read_node(RTC_SOURCE, buf, sizeof(buf));

    in->source =
        n < 0 ? TG_TIME_SRC_UNKNOWN : tg_time_parse_source(buf, (size_t)n);
    in->engine_valid = false;

    n = read_node(RTC_STATUS, buf, sizeof(buf));
    if (n < 0 || strncmp(buf, "valid", 5) != 0) {
        return;
    }

    n = read_node(RTC_TIME, buf, sizeof(buf));
    if (n > 0) {
        in->engine_s = (uint32_t)strtoul(buf, NULL, 10);
        in->engine_valid = true;
    }
}

static void read_config(void) {
    char buf[128];
    int n = read_node(CONFIG_PATH, buf, sizeof(buf));

    if (n <= 0) {
        return;
    }

    if (tg_time_parse_offset(buf, (size_t)n, &g_offset_min) < 0) {
        note("tg-time: the offset of the config is not valid, using 0\n");
    }
}

/****************************************************************************
 * The loop
 ****************************************************************************/

static void step(void) {
    struct tg_time_in in;
    struct tg_time_action act;
    uint16_t frames = 0;

    memset(&in, 0, sizeof(in));
    read_engine(&in);

    in.stm_known = read_stm(&in.stm_s, &frames);
    if (!in.stm_known) {
        note("tg-time: the STM32 sent no state\n");
    } else {
        if (tg_time_stm_reset(g_have_frames, g_frames, frames)) {
            g_offset_pending = true;
        }

        g_frames = frames;
        g_have_frames = true;
    }

    in.offset_due = g_offset_pending;
    act = tg_time_decide(&in);

    if (act.kind == TG_TIME_WRITE_ENGINE) {
        write_engine(act.seconds);
    } else if (act.kind == TG_TIME_PUSH_STM && push_stm(act.seconds)) {
        g_offset_pending = false;
    }
}

int main(void) {
    const char *name = getenv(PEER_ENV);
    const char *text = getenv(INTERVAL_ENV);
    const char *loops = getenv(LOOPS_ENV);
    unsigned long interval = text != NULL ? strtoul(text, NULL, 10) : 0;
    unsigned long left = loops != NULL ? strtoul(loops, NULL, 10) : 0;
    char probe[16];

    if (name != NULL && name[0] != '\0') {
        snprintf(g_name, sizeof(g_name), "%s", name);
    }

    if (interval == 0) {
        interval = INTERVAL_DEFAULT_S;
    }

    read_config();
    if (open_pipes() < 0) {
        note("tg-time: the broker has no pipes for this peer\n");
        return 1;
    }

    if (read_node(RTC_SOURCE, probe, sizeof(probe)) < 0) {
        note("tg-time: no rtc grant\n");
        return 1;
    }

    ipc_parser_init(&g_parser);
    for (;;) {
        step();

        if (loops != NULL && --left == 0) {
            break;
        }

        sleep((unsigned int)interval);
    }

    return 0;
}
