/* SPDX-License-Identifier: Apache-2.0 */

/* The firmware of the STM32 travels in this wapp's own image and goes in
 * through the bootloader of the ROM. The broker holds the line, thus this
 * takes raw mode from it and drives BOOT0 and NRST through its own grant.
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

#define PEER_ENV "TELEGRAPH_PEER"
#define PEER_DEFAULT "ota"

/* The image and its version travel in the root of this wapp. */

#define IMAGE_PATH "/firmware.bin"
#define VERSION_PATH "/firmware.version"

#define GPIO_BOOT0 "/dev/gpio/boot0/value"
#define GPIO_NRST "/dev/gpio/nrst/value"

/* The rate and the format of the bootloader of the ROM. */

#define BOOT_BAUD 57600u
#define BOOT_DATABITS 8u
#define BOOT_PARITY 'E'
#define BOOT_STOPBITS 1u

/* The sequence of the reset, in milliseconds. These are the values the
 * bring-up bridge proved on this hardware.
 */

#define BOOT0_SETTLE_MS 50u
#define RESET_HOLD_MS 100u
#define BOOT_SETTLE_MS 500u
#define RUN_SETTLE_MS 200u

/* AN3155. */

#define AN_ACK 0x79u
#define AN_NACK 0x1fu
#define AN_SYNC 0x7fu

/* The bootloader auto-bauds on the sync byte, and the first one after a reset
 * is lost often enough that a single attempt is not a test of whether the
 * bootloader is there.
 */

#define AN_SYNC_TRIES 8u
#define AN_SYNC_GAP_MS 50u
#define AN_GET_ID 0x02u
#define AN_ERASE 0x43u
#define AN_WRITE 0x31u

#define FLASH_ORIGIN 0x08000000u
#define CHUNK 256u

/* One answer of the bootloader, and one whole run. */

#define REPLY_MS 2000u
#define RAW_MS 5000u

#define POLL_US 10000u

/* The board answers the version query while another peer holds the broker,
 * thus a single NACK says nothing about the firmware that runs.
 */

#define VERSION_TRIES 10u
#define VERSION_RETRY_MS 1000u

/* The whole budget of one exchange with the board. The broker refuses a
 * request while it holds another, thus this covers a link in use by the
 * wapps that start beside this one.
 */

#define ASK_TOTAL_MS 20000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_name[TG_BRK_NAME_MAX + 1] = PEER_DEFAULT;
static int g_req = -1;
static int g_rsp = -1;

static struct ipc_parser_s g_parser;
static uint8_t g_frame[IPC_FRAME_MAX];

/* What raw mode carried back, and what a reply of the broker holds. */

static uint8_t g_line[512];
static unsigned int g_lineLen;

static uint8_t g_reply[64];
static unsigned int g_replyLen;
static uint8_t g_replyOp;
static bool g_gotReply;

/* The identifier of the request in flight. Each one takes a new value, thus a
 * reply that arrives after its own request timed out matches nothing and is
 * dropped instead of answering the request that follows.
 */

static uint16_t g_corr = 1;
static uint16_t g_inflight;

static uint32_t g_addr = FLASH_ORIGIN;

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

static void delay_ms(unsigned int ms) {
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (long)(ms % 1000) * 1000000};

    nanosleep(&ts, NULL);
}

/* A frame of the broker: raw bytes go to the buffer of the line, and anything
 * else is the reply of a request.
 */

static void on_frame(void *arg, const struct ipc_frame_s *frame) {
    (void)arg;

    if (frame->opcode == TG_BRK_OP_RAW_DATA) {
        unsigned int room = (unsigned int)sizeof(g_line) - g_lineLen;
        unsigned int len = frame->payload_len;

        if (len > room) {
            len = room;
        }

        memcpy(&g_line[g_lineLen], frame->payload, len);
        g_lineLen += len;
        return;
    }

    if (frame->corr_id != g_inflight) {
        return; /* the answer of a request that timed out */
    }

    g_replyOp = frame->opcode;
    g_replyLen = frame->payload_len;
    if (g_replyLen > sizeof(g_reply)) {
        g_replyLen = (unsigned int)sizeof(g_reply);
    }

    memcpy(g_reply, frame->payload, g_replyLen);
    g_gotReply = true;
}

/* Read whatever the broker holds for this wapp. */

static void pump(void) {
    uint8_t buf[256];
    ssize_t n = read(g_rsp, buf, sizeof(buf));

    if (n > 0) {
        ipc_parser_push(&g_parser, buf, (size_t)n, on_frame, NULL);
    }
}

static int send(uint8_t opcode, uint16_t corr_id, const void *payload,
                uint16_t len) {
    int n = ipc_encode(g_frame, sizeof(g_frame), opcode, corr_id, payload, len);

    if (n < 0 || write(g_req, g_frame, (size_t)n) != n) {
        emit("ota: the broker took no frame\n");
        return -1;
    }

    return 0;
}

/* Give a request the next identifier and send it. */

static int send_request(uint8_t opcode, const void *payload, uint16_t len) {
    if (++g_corr == IPC_CORR_ID_PUSH) {
        g_corr = 1;
    }

    g_inflight = g_corr;
    g_gotReply = false;
    return send(opcode, g_inflight, payload, len);
}

/* Send a request and wait for the reply that carries its identifier. */

static int request(uint8_t opcode, const void *payload, uint16_t len,
                   unsigned int wait_ms) {
    uint64_t deadline;

    if (send_request(opcode, payload, len) < 0) {
        return -1;
    }

    deadline = now_ms() + wait_ms;
    while (!g_gotReply && now_ms() < deadline) {
        pump();
        if (!g_gotReply) {
            nap();
        }
    }

    return g_gotReply ? 0 : -1;
}

/* Keep asking while the broker answers that its line is in use; a request
 * that goes unanswered is waited for, since a second frame in the pipe takes
 * a NACK of its own. Returns 0 with the reply in g_reply, or -1.
 */

static int ask_until(uint8_t opcode, const void *payload, uint16_t len,
                     unsigned int total_ms) {
    uint64_t deadline = now_ms() + total_ms;
    unsigned int asks = 0;

    while (now_ms() < deadline) {
        asks++;
        if (send_request(opcode, payload, len) < 0) {
            return -1;
        }

        while (!g_gotReply && now_ms() < deadline) {
            pump();
            if (!g_gotReply) {
                nap();
            }
        }

        if (!g_gotReply) {
            emitf("ota: no answer to 0x%02x after %u ask(s)\n", opcode, asks);
            return -1;
        }

        if (g_replyOp != IPC_OP_NACK || g_replyLen == 0 ||
            g_reply[0] != IPC_ERR_BUSY) {
            return 0;
        }

        delay_ms(VERSION_RETRY_MS);
    }

    emitf("ota: the line stayed in use for 0x%02x after %u ask(s)\n", opcode,
          asks);
    return -1;
}

/****************************************************************************
 * The pins
 ****************************************************************************/

static int pin_write(const char *path, bool level) {
    int fd = open(path, O_WRONLY);
    int n;

    if (fd < 0) {
        emitf("ota: %s is out of reach\n", path);
        return -1;
    }

    n = (int)write(fd, level ? "1" : "0", 1);
    close(fd);
    return n == 1 ? 0 : -1;
}

/* Start the target, in the bootloader of the ROM or in its own firmware. */

static int reset_target(bool bootloader) {
    if (pin_write(GPIO_BOOT0, bootloader) < 0) {
        return -1;
    }

    delay_ms(BOOT0_SETTLE_MS);

    if (pin_write(GPIO_NRST, false) < 0) {
        return -1;
    }

    delay_ms(RESET_HOLD_MS);

    if (pin_write(GPIO_NRST, true) < 0) {
        return -1;
    }

    delay_ms(bootloader ? BOOT_SETTLE_MS : RUN_SETTLE_MS);
    return 0;
}

/****************************************************************************
 * AN3155
 ****************************************************************************/

static int raw_enter(void) {
    uint8_t settings[TG_BRK_RAW_LEN];

    ipc_put_u32(&settings[TG_BRK_RAW_BAUD], BOOT_BAUD);
    settings[TG_BRK_RAW_DATABITS] = BOOT_DATABITS;
    settings[TG_BRK_RAW_PARITY] = BOOT_PARITY;
    settings[TG_BRK_RAW_STOPBITS] = BOOT_STOPBITS;

    if (ask_until(TG_BRK_OP_RAW, settings, sizeof(settings), ASK_TOTAL_MS) <
            0 ||
        g_replyOp != IPC_OP_ACK) {
        emit("ota: the broker kept the line\n");
        return -1;
    }

    g_lineLen = 0;
    return 0;
}

static int raw_leave(void) { return request(TG_BRK_OP_RAW, NULL, 0, RAW_MS); }

static int line_write(const void *data, uint16_t len) {
    return send(TG_BRK_OP_RAW_DATA, IPC_CORR_ID_PUSH, data, len);
}

/* Wait for the ACK of the bootloader. Every other byte is noise of the line,
 * which a reset leaves behind.
 */

/* The first ACK or NACK the line gives, or -1 when none arrives in time. */

static int wait_byte(void) {
    uint64_t deadline = now_ms() + REPLY_MS;

    for (;;) {
        while (g_lineLen > 0) {
            uint8_t b = g_line[0];

            memmove(g_line, &g_line[1], --g_lineLen);
            if (b == AN_ACK || b == AN_NACK) {
                return (int)b;
            }
        }

        if (now_ms() >= deadline) {
            return -1;
        }

        pump();
        nap();
    }
}

static int wait_ack(void) { return wait_byte() == AN_ACK ? 0 : -1; }

/* Take one byte of the line. */

static int line_byte(uint8_t *out) {
    uint64_t deadline = now_ms() + REPLY_MS;

    while (g_lineLen == 0) {
        if (now_ms() >= deadline) {
            return -1;
        }

        pump();
        nap();
    }

    *out = g_line[0];
    memmove(g_line, &g_line[1], --g_lineLen);
    return 0;
}

static int an_command(uint8_t cmd) {
    uint8_t bytes[2] = {cmd, (uint8_t)(0xffu ^ cmd)};

    if (line_write(bytes, sizeof(bytes)) < 0) {
        return -1;
    }

    return wait_ack();
}

static int an_sync(void) {
    unsigned int i;

    for (i = 0; i < AN_SYNC_TRIES; i++) {
        uint8_t byte = AN_SYNC;
        int reply;

        g_lineLen = 0;
        if (line_write(&byte, 1) < 0) {
            return -1;
        }

        /* A bootloader already initialised answers NACK to a second sync,
         * which is still a bootloader that is listening.
         */

        reply = wait_byte();
        if (reply == AN_ACK || reply == AN_NACK) {
            return 0;
        }

        delay_ms(AN_SYNC_GAP_MS);
    }

    return -1;
}

/* The identifier of the part, which proves the bootloader answers. */

static int an_get_id(uint16_t *pid) {
    uint8_t count = 0;
    uint16_t id = 0;

    if (an_command(AN_GET_ID) < 0 || line_byte(&count) < 0) {
        return -1;
    }

    for (unsigned int i = 0; i <= count; i++) {
        uint8_t b = 0;

        if (line_byte(&b) < 0) {
            return -1;
        }

        id = (uint16_t)((id << 8) | b);
    }

    *pid = id;
    return wait_ack();
}

static int an_erase(void) {
    uint8_t all[2] = {0xff, 0x00};

    if (an_command(AN_ERASE) < 0 || line_write(all, sizeof(all)) < 0) {
        return -1;
    }

    return wait_ack();
}

static int an_write(const uint8_t *data, uint16_t len) {
    uint8_t head[5];
    uint8_t body[CHUNK + 2];
    uint8_t sum = 0;

    if (an_command(AN_WRITE) < 0) {
        return -1;
    }

    head[0] = (uint8_t)(g_addr >> 24);
    head[1] = (uint8_t)(g_addr >> 16);
    head[2] = (uint8_t)(g_addr >> 8);
    head[3] = (uint8_t)g_addr;
    head[4] = (uint8_t)(head[0] ^ head[1] ^ head[2] ^ head[3]);

    if (line_write(head, sizeof(head)) < 0 || wait_ack() < 0) {
        return -1;
    }

    body[0] = (uint8_t)(len - 1);
    sum = body[0];
    for (uint16_t i = 0; i < len; i++) {
        body[1 + i] = data[i];
        sum ^= data[i];
    }

    body[1 + len] = sum;

    if (line_write(body, (uint16_t)(len + 2)) < 0 || wait_ack() < 0) {
        return -1;
    }

    g_addr += len;
    return 0;
}

/****************************************************************************
 * The image and the version
 ****************************************************************************/

/* Read a whole file of the root of this wapp. */

static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *len) {
    int fd = open(path, O_RDONLY);
    size_t got = 0;

    if (fd < 0) {
        return -1;
    }

    for (;;) {
        ssize_t n = read(fd, &buf[got], cap - got);

        if (n <= 0) {
            break;
        }

        got += (size_t)n;
        if (got == cap) {
            break;
        }
    }

    close(fd);
    *len = got;
    return 0;
}

/* The version text of the state the last reply holds. */

static int version_from_state(char *out, size_t cap) {
    size_t n = g_replyLen - IPC_STATE_FWVER;

    if (n >= cap) {
        n = cap - 1;
    }

    memcpy(out, &g_reply[IPC_STATE_FWVER], n);
    out[n] = '\0';
    return 0;
}

/* The version the running firmware reports, through the broker.
 *
 * Note: this decides whether the flash is written, and the broker answers a
 * busy link with a NACK. Thus only that NACK is asked again.
 */

static int running_version(char *out, size_t cap) {
    if (ask_until(IPC_OP_GET_STATE, NULL, 0, ASK_TOTAL_MS) < 0) {
        return -1;
    }

    if (g_replyOp != IPC_OP_STATE) {
        emitf("ota: the state was refused (op 0x%02x, code %u)\n", g_replyOp,
              g_replyLen > 0 ? g_reply[0] : 0);
        return -1;
    }

    if (g_replyLen <= IPC_STATE_FWVER) {
        emit("ota: the board answered with a state that carries no version\n");
        return -1;
    }

    return version_from_state(out, cap);
}

/****************************************************************************
 * The peer
 ****************************************************************************/

static int open_pipes(void) {
    char path[64];
    uint64_t deadline = now_ms() + RAW_MS;

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

/* Write the whole image through the bootloader.
 *
 * Note: the image is read a chunk at a time. A wapp holds 64 KiB of linear
 * memory, thus a buffer of the whole firmware would not fit beside it.
 */

static int write_image(int fd, size_t len) {
    uint16_t pid = 0;
    size_t off = 0;

    if (reset_target(true) < 0) {
        emit("ota: the pins of the target are out of reach\n");
        return -1;
    }

    if (an_sync() < 0) {
        emit("ota: the bootloader did not answer\n");
        return -1;
    }

    if (an_get_id(&pid) < 0) {
        emit("ota: the part did not give its identifier\n");
        return -1;
    }

    emitf("ota: the bootloader answers, part 0x%03x\n", pid);

    if (an_erase() < 0) {
        emit("ota: the erase failed\n");
        return -1;
    }

    g_addr = FLASH_ORIGIN;
    while (off < len) {
        uint8_t chunk[CHUNK];
        ssize_t got = read(fd, chunk, sizeof(chunk));
        size_t n;

        if (got <= 0) {
            emit("ota: the image ended early\n");
            return -1;
        }

        n = (size_t)got;

        /* A write takes whole words, and an erased byte reads as 0xff, thus
         * the last chunk takes that value to its next word.
         */

        while (n % 4 != 0) {
            chunk[n++] = 0xff;
        }

        if (an_write(chunk, (uint16_t)n) < 0) {
            emitf("ota: the write stopped at 0x%08x\n", (unsigned)g_addr);
            return -1;
        }

        off += (size_t)got;
    }

    emitf("ota: %u bytes written\n", (unsigned)len);
    return 0;
}

/* Write the image, and leave the target running its own firmware either way.
 * A failure that returned with BOOT0 still high left the board in the ROM
 * bootloader, where nothing on the link answers and the display stays dark.
 */

static int flash(int fd, size_t len) {
    int rc;

    if (raw_enter() < 0) {
        return -1;
    }

    rc = write_image(fd, len);

    if (reset_target(false) < 0) {
        emit("ota: the target was not restored\n");
        return -1;
    }

    return rc;
}

/* The length of a file of the root of this wapp. */

static long file_size(const char *path) {
    int fd = open(path, O_RDONLY);
    long len = 0;

    if (fd < 0) {
        return -1;
    }

    for (;;) {
        uint8_t buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n <= 0) {
            break;
        }

        len += n;
    }

    close(fd);
    return len;
}

int main(void) {
    const char *name = getenv(PEER_ENV);
    char want[32] = "";
    char have[32] = "";
    long len = 0;
    size_t vlen = 0;
    int fd = -1;
    int rc = 1;

    if (name != NULL && name[0] != '\0') {
        snprintf(g_name, sizeof(g_name), "%s", name);
    }

    ipc_parser_init(&g_parser);

    len = file_size(IMAGE_PATH);
    if (len <= 0) {
        emit("ota: this image carries no firmware of the STM32\n");
        return 1;
    }

    if (read_file(VERSION_PATH, (uint8_t *)want, sizeof(want) - 1, &vlen) ==
        0) {
        want[vlen] = '\0';
        while (vlen > 0 && (want[vlen - 1] == '\n' || want[vlen - 1] == '\r')) {
            want[--vlen] = '\0';
        }
    }

    if (open_pipes() < 0) {
        emit("ota: the pipes of the broker stayed closed\n");
        return 1;
    }

    /* A restart of this wapp must not write the flash again, thus the version
     * of the running firmware decides. An exact match is what the protocol
     * document states, thus a build with local changes always writes.
     */

    if (running_version(have, sizeof(have)) == 0) {
        emitf("ota: the board runs %s, this image carries %s\n", have,
              want[0] != '\0' ? want : "no version");
        if (want[0] != '\0' && strcmp(want, have) == 0) {
            emit("ota: the board already runs this image\n");
            return 0;
        }
    } else {
        /* Writing the flash blind is the more dangerous action, and a new
         * firmware of the STM32 arrives only as a new version of this wapp,
         * which runs this check again. Thus nothing is written here.
         */

        emit("ota: the board gave no version, thus nothing is written\n");
        return 0;
    }

    fd = open(IMAGE_PATH, O_RDONLY);
    if (fd < 0) {
        emit("ota: the firmware of the STM32 did not open\n");
        return 1;
    }

    if (flash(fd, (size_t)len) == 0) {
        rc = 0;
    }

    close(fd);

    raw_leave();

    if (rc == 0 && running_version(have, sizeof(have)) == 0) {
        emitf("ota: the board now runs %s\n", have);
        rc = (want[0] == '\0' || strcmp(want, have) == 0) ? 0 : 1;
    }

    return rc;
}
