// SPDX-License-Identifier: Apache-2.0

#include <Arduino.h>

#include <telegraph/ipc.h>

#include "ipc_peer.h"

// The pins come from the build flags. Refer to platformio.ini.
#ifndef STM_TX_PIN
#  define STM_TX_PIN 1
#endif
#ifndef STM_RX_PIN
#  define STM_RX_PIN 2
#endif

// The line is idle after this period without a byte.
#define IPC_IDLE_MS 20

// A request gets no more time than this before the peer sends it again.
#define IPC_REPLY_MS 500

// The count of the attempts for one request, the first one included.
//
// Note: every operation of this protocol gives the same result when it runs
// twice. Thus a request that lost its frame goes out again without harm, and
// a write of a file carries both marks in one frame.
#define IPC_ATTEMPTS 3

static struct ipc_parser_s g_parser;
static uint8_t g_tx[IPC_FRAME_MAX];
static bool g_active = false;

// The peer gives each request its own ID. The value 0 marks a push frame,
// thus the counter starts at 1.
static uint16_t g_nextCorrId = 1;

// These values hold the result of the last request.
static uint16_t g_waitCorrId = 0;
static bool g_replied = false;

// The allowance of the sender. Each frame takes one credit, and each ACK
// gives the current count of the receiver.
//
// Note: the protocol has no initial grant. Thus the peer starts with one
// frame, and it learns the true capacity from the first ACK.
static uint8_t g_credits = 1;
static uint8_t g_minCredits = 0xff;
static uint32_t g_stalls = 0;

// The count of the requests that went out again after a timeout.
static uint32_t g_retries = 0;

static unsigned long g_lastByteMs = 0;

// A burst gives one line for each frame. This flag keeps that output away.
static bool g_quiet = false;

// The output goes to the USB console. A caller over the network also captures
// it, thus the peer runs without a USB connection.
static String g_capture;
static bool g_capturing = false;

static void ipcOut(const char *fmt, ...) {
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    Serial.print(buf);
    if (g_capturing) g_capture += buf;
}

static uint16_t ipcNextCorrId() {
    if (++g_nextCorrId == 0) g_nextCorrId = 1;
    return g_nextCorrId;
}

static void ipcReport(void *arg, const struct ipc_frame_s *frame) {
    (void)arg;

    if (frame->corr_id == IPC_CORR_ID_PUSH) {
        // A push frame answers no request.
        if (frame->opcode == IPC_OP_LOG) {
            ipcOut("PUSH log \"%.*s\"\n", (int)frame->payload_len,
                          (const char *)frame->payload);
        } else {
            ipcOut("PUSH op=0x%02X len=%u\n", frame->opcode,
                          frame->payload_len);
        }
        return;
    }

    if (frame->corr_id == g_waitCorrId) g_replied = true;

    switch (frame->opcode) {
        case IPC_OP_ACK:
            g_credits = frame->payload_len ? frame->payload[0] : 0;
            if (g_credits < g_minCredits) g_minCredits = g_credits;
            if (!g_quiet)
                ipcOut("ACK  id=%u credits=%u\n", frame->corr_id, g_credits);
            break;

        case IPC_OP_NACK:
            ipcOut("NACK id=%u error=0x%02X\n", frame->corr_id,
                          frame->payload_len ? frame->payload[0] : 0);
            break;

        case IPC_OP_STATE: {
            if (frame->payload_len < IPC_STATE_LEN) {
                ipcOut("ERR state length %u\n", frame->payload_len);
                break;
            }

            // The bytes after the fixed fields are the firmware version, and
            // they carry no terminator.
            char fw[IPC_FWVER_MAX + 1];
            uint16_t fwlen = frame->payload_len - IPC_STATE_LEN;

            if (fwlen > IPC_FWVER_MAX) {
                fwlen = IPC_FWVER_MAX;
            }

            memcpy(fw, &frame->payload[IPC_STATE_FWVER], fwlen);
            fw[fwlen] = '\0';

            ipcOut(
                "STATE id=%u time=%lu temp=%d.%dC frames=%u crcerr=%u "
                "resync=%u ver=%u fw=%s\n",
                frame->corr_id,
                (unsigned long)ipc_get_u32(&frame->payload[IPC_STATE_TIME]),
                (int)((int16_t)ipc_get_u16(&frame->payload[IPC_STATE_TEMP])) / 10,
                abs((int)((int16_t)ipc_get_u16(&frame->payload[IPC_STATE_TEMP])) % 10),
                ipc_get_u16(&frame->payload[IPC_STATE_FRAMES]),
                ipc_get_u16(&frame->payload[IPC_STATE_CRC_ERR]),
                frame->payload[IPC_STATE_RESYNC],
                frame->payload[IPC_STATE_VERSION],
                fw);
            break;
        }

        default:
            ipcOut("RX   op=0x%02X id=%u len=%u\n", frame->opcode,
                          frame->corr_id, frame->payload_len);
            break;
    }
}

// Transmit a frame, then wait for the reply of that correlation ID. A request
// without a reply goes out again.
//
// Note: every attempt carries the same correlation ID. Thus a late reply to an
// earlier attempt still answers this request.
static void ipcRequest(uint8_t opcode, const void *payload, uint16_t len) {
    if (!g_active) {
        ipcOut("ERR ipc is off\n");
        return;
    }

    uint16_t id = ipcNextCorrId();
    int n = ipc_encode(g_tx, sizeof(g_tx), opcode, id, payload, len);

    if (n < 0) {
        ipcOut("ERR encode %d\n", n);
        return;
    }

    for (int attempt = 1; attempt <= IPC_ATTEMPTS; attempt++) {
        if (g_credits == 0) {
            // The receiver has no space. Wait for an ACK to give some.
            unsigned long until = millis() + IPC_REPLY_MS;

            while (g_credits == 0 && (long)(millis() - until) < 0) {
                ipcPoll();
            }

            if (g_credits == 0) {
                ipcOut("ERR no credits, wait for an ACK\n");
                return;
            }
        }

        g_waitCorrId = id;
        g_replied = false;
        Serial1.write(g_tx, (size_t)n);

        unsigned long deadline = millis() + IPC_REPLY_MS;
        while (!g_replied && (long)(millis() - deadline) < 0) {
            ipcPoll();
        }

        if (g_replied) {
            if (attempt > 1) {
                ipcOut("INFO id=%u answered on attempt %d\n", id, attempt);
            }
            return;
        }

        g_retries++;
    }

    ipcOut("ERR no reply for id=%u after %d attempts\n", id, IPC_ATTEMPTS);
}

// Transmit bytes that hold no valid frame, then transmit a good frame.
//
// Note: this test shows the recovery of the parser. The noise gives a false
// start-of-frame byte, and the idle timeout removes that candidate.
static void ipcNoise() {
    uint8_t noise[5];

    memset(noise, IPC_SOF, sizeof(noise));
    Serial1.write(noise, sizeof(noise));

    ipcOut("INFO sent 5 false SOF bytes\n");
    ipcRequest(IPC_OP_GET_STATE, NULL, 0);
}

// Transmit one frame that carries a text for the sub panel.
static int ipcSendText(unsigned int i) {
    uint16_t id = ipcNextCorrId();
    uint8_t payload[IPC_TEXT_BODY + 3];
    int n;

    payload[IPC_TEXT_ATTRS] = IPC_ALIGN_CENTRE;
    snprintf((char *)&payload[IPC_TEXT_BODY], 4, "%03u", i % 1000);

    n = ipc_encode(g_tx, sizeof(g_tx), IPC_OP_SET_SMALL, id, payload,
                   sizeof(payload));

    if (n > 0) Serial1.write(g_tx, (size_t)n);
    return n;
}

// Transmit frames, and obey the credits.
//
// Note: this test shows the flow control. The sender stops at zero credits,
// and it starts again at the next ACK. Only an ACK carries the count.
static void ipcBurst(unsigned int count) {
    unsigned long t0 = millis();
    unsigned int sent = 0;

    g_minCredits = 0xff;
    g_stalls = 0;
    g_quiet = true;

    while (sent < count) {
        if (g_credits == 0) {
            // The receiver has no space. Wait for an ACK.
            unsigned long deadline = millis() + IPC_REPLY_MS;

            g_stalls++;
            while (g_credits == 0 && (long)(millis() - deadline) < 0) {
                ipcPoll();
            }

            if (g_credits == 0) break;
            continue;
        }

        int n = ipcSendText(sent);

        if (n > 0) {
            uint8_t cost = (uint8_t)IPC_FRAME_CREDITS(IPC_TEXT_BODY + 3);

            g_credits = (g_credits > cost) ? (uint8_t)(g_credits - cost) : 0;
            sent++;
        }

        ipcPoll();
    }

    unsigned long deadline = millis() + IPC_REPLY_MS;
    while ((long)(millis() - deadline) < 0) ipcPoll();

    g_quiet = false;
    ipcOut("INFO sent=%u in %lums min_credits=%u stalls=%lu\n", sent,
           millis() - t0, g_minCredits, (unsigned long)g_stalls);
    ipcOut("INFO retries=%lu\n", (unsigned long)g_retries);
    ipcOut("INFO parser frames=%lu crcerr=%lu badlen=%lu resync=%lu\n",
           (unsigned long)g_parser.stats.frames,
           (unsigned long)g_parser.stats.crc_errors,
           (unsigned long)g_parser.stats.bad_length,
           (unsigned long)g_parser.stats.resyncs);
}

// Transmit frames without any regard for the credits.
//
// Note: this test shows the condition that the flow control prevents. The
// receive buffer of the target fills, and the data that comes after is lost.
static void ipcFlood(unsigned int count) {
    unsigned long t0 = millis();

    g_minCredits = 0xff;
    g_quiet = true;

    for (unsigned int i = 0; i < count; i++) ipcSendText(i);

    unsigned long deadline = millis() + 2000;
    while ((long)(millis() - deadline) < 0) ipcPoll();

    g_quiet = false;
    ipcOut("INFO flooded %u frames in %lums, min_credits=%u\n", count,
           millis() - t0, g_minCredits);
    ipcOut("INFO replies=%lu crcerr=%lu badlen=%lu resync=%lu\n",
           (unsigned long)g_parser.stats.frames,
           (unsigned long)g_parser.stats.crc_errors,
           (unsigned long)g_parser.stats.bad_length,
           (unsigned long)g_parser.stats.resyncs);
}

// Reset the STM32, and keep the peer active.
//
// Note: the STM32 transmits a log frame at its start. Thus this command shows
// a push frame.

// Send a text to one panel. The form is "[-l|-c|-r ]<text>", and the flag
// gives the place of the text across the panel. Without a flag the text goes
// in the middle.
static void ipcText(uint8_t opcode, const char *text)
{
    static uint8_t payload[IPC_TEXT_BODY + 256];
    uint8_t align = IPC_ALIGN_CENTRE;

    if (strncmp(text, "-l ", 3) == 0) {
        align = IPC_ALIGN_LEFT;
        text += 3;
    } else if (strncmp(text, "-r ", 3) == 0) {
        align = IPC_ALIGN_RIGHT;
        text += 3;
    } else if (strncmp(text, "-c ", 3) == 0) {
        text += 3;
    }

    size_t len = strlen(text);

    if (len > sizeof(payload) - IPC_TEXT_BODY) {
        len = sizeof(payload) - IPC_TEXT_BODY;
    }

    payload[IPC_TEXT_ATTRS] = align;
    memcpy(&payload[IPC_TEXT_BODY], text, len);

    ipcRequest(opcode, payload, (uint16_t)(IPC_TEXT_BODY + len));
}

static void ipcReset() {
    digitalWrite(PIN_BOOT0, LOW);
    delay(20);
    digitalWrite(PIN_RST, LOW);
    delay(100);
    digitalWrite(PIN_RST, HIGH);

    ipcOut("INFO target reset, listening for a push\n");

    unsigned long deadline = millis() + 2000;
    while ((long)(millis() - deadline) < 0) ipcPoll();
}

void ipcBegin(unsigned long baud) {
    Serial1.begin(baud, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);
    ipc_parser_init(&g_parser);
    g_active = true;
    g_credits = 1;
    g_minCredits = 0xff;
    ipcOut("OK ipc on at %lu baud\n", baud);
}

void ipcEnd() {
    g_active = false;
    ipcOut("OK ipc off\n");
}

bool ipcActive() { return g_active; }

void ipcPoll() {
    if (!g_active) return;

    while (Serial1.available()) {
        uint8_t buf[128];
        int avail = Serial1.available();

        if (avail > (int)sizeof(buf)) avail = sizeof(buf);
        size_t len = Serial1.readBytes(buf, avail);
        ipc_parser_push(&g_parser, buf, len, ipcReport, NULL);
        g_lastByteMs = millis();
    }

    // The line is idle. A false start-of-frame byte holds the parser, thus
    // this call discards that candidate.
    if (ipc_parser_pending(&g_parser) &&
        millis() - g_lastByteMs > IPC_IDLE_MS) {
        ipc_parser_timeout(&g_parser, ipcReport, NULL);
        g_lastByteMs = millis();
    }
}

void ipcHelp() {
    ipcOut("INFO ipc: on [baud] | off | state | time <epoch> | large <text> |\n");
    ipcOut("INFO      small <text> | clear | bad | badlen | reset | noise |\n");
    ipcOut("INFO      burst <n> | stats\n");
}

// Run a command, and give the output back as text.
String ipcRun(const char *args) {
    g_capture = "";
    g_capturing = true;
    ipcCommand(args);
    g_capturing = false;
    return g_capture;
}

void ipcCommand(const char *args) {
    if (strncmp(args, "on", 2) == 0) {
        unsigned long baud = strtoul(args + 2, NULL, 10);

        ipcBegin(baud ? baud : 115200);
    } else if (strcmp(args, "off") == 0) {
        ipcEnd();
    } else if (strcmp(args, "state") == 0) {
        ipcRequest(IPC_OP_GET_STATE, NULL, 0);
    } else if (strncmp(args, "time ", 5) == 0) {
        // The form is "time <epoch> [offset]". The epoch is UTC, and the
        // offset gives the minutes of the local time from UTC.
        uint8_t payload[IPC_SET_TIME_TZ_LEN];
        char *end = NULL;
        uint32_t epoch = (uint32_t)strtoul(args + 5, &end, 10);
        uint16_t len = IPC_SET_TIME_LEN;

        ipc_put_u32(&payload[IPC_SET_TIME_UTC], epoch);

        if (end != NULL && *end != '\0') {
            long offset = strtol(end, NULL, 10);

            ipc_put_u16(&payload[IPC_SET_TIME_OFFSET], (uint16_t)(int16_t)offset);
            len = IPC_SET_TIME_TZ_LEN;
        }

        ipcRequest(IPC_OP_SET_TIME, payload, len);
    } else if (strncmp(args, "large ", 6) == 0) {
        ipcText(IPC_OP_SET_LARGE, args + 6);
    } else if (strncmp(args, "small ", 6) == 0) {
        ipcText(IPC_OP_SET_SMALL, args + 6);
    } else if (strncmp(args, "bright ", 7) == 0) {
        // The form is "bright <digits> [panels]". Permitted values are 0 to
        // 8. The value 0 turns the device off.
        uint8_t payload[IPC_SET_BRIGHT2_LEN];
        char *end = NULL;
        uint16_t len = IPC_SET_BRIGHT_LEN;

        payload[0] = (uint8_t)strtoul(args + 7, &end, 10);

        if (end != NULL && *end != '\0') {
            payload[1] = (uint8_t)strtoul(end, NULL, 10);
            len = IPC_SET_BRIGHT2_LEN;
        }

        ipcRequest(IPC_OP_SET_BRIGHT, payload, len);
    } else if (strncmp(args, "tempoff ", 8) == 0) {
        // The form is "tempoff <tenths>". The value carries a sign.
        uint8_t payload[IPC_SET_TEMPOFF_LEN];
        int16_t tenths = (int16_t)strtol(args + 8, NULL, 10);

        ipc_put_u16(payload, (uint16_t)tenths);
        ipcRequest(IPC_OP_SET_TEMPOFF, payload, IPC_SET_TEMPOFF_LEN);
    } else if (strncmp(args, "sleep ", 6) == 0) {
        // The form is "sleep <start> <end>", and each time is HH or HH:MM.
        // The word "off" stops this function.
        uint8_t payload[IPC_SET_SLEEP_LEN];
        uint16_t start = IPC_SLEEP_OFF;
        uint16_t end = IPC_SLEEP_OFF;
        const char *p = args + 6;

        if (strncmp(p, "off", 3) != 0) {
            char *tail = NULL;
            long h = strtol(p, &tail, 10);
            long m = (tail != NULL && *tail == ':') ? strtol(tail + 1, &tail, 10) : 0;
            start = (uint16_t)(h * 60 + m);

            h = strtol(tail, &tail, 10);
            m = (tail != NULL && *tail == ':') ? strtol(tail + 1, &tail, 10) : 0;
            end = (uint16_t)(h * 60 + m);
        }

        ipc_put_u16(&payload[IPC_SLEEP_START], start);
        ipc_put_u16(&payload[IPC_SLEEP_END], end);
        ipcRequest(IPC_OP_SET_SLEEP, payload, IPC_SET_SLEEP_LEN);
    } else if (strncmp(args, "putfile ", 8) == 0) {
        // The form is "putfile <path> <hex>". One command carries the whole
        // file, thus the size fits in one frame.
        char path[IPC_ASSET_PATH_MAX + 1];
        const char *p = args + 8;
        const char *space = strchr(p, ' ');
        size_t pathlen = (space != NULL) ? (size_t)(space - p) : 0;

        if (pathlen == 0 || pathlen > IPC_ASSET_PATH_MAX) {
            ipcOut("ERR path\n");
            return;
        }

        memcpy(path, p, pathlen);
        path[pathlen] = '\0';

        const char *hex = space + 1;
        size_t hexlen = strlen(hex);

        if ((hexlen % 2) != 0) {
            ipcOut("ERR hex\n");
            return;
        }

        size_t datalen = hexlen / 2;
        size_t total = IPC_ASSET_PATH + pathlen + datalen;

        if (total > IPC_MAX_PAYLOAD) {
            ipcOut("ERR too large: %u bytes\n", (unsigned)total);
            return;
        }

        static uint8_t payload[IPC_MAX_PAYLOAD];

        payload[IPC_ASSET_FLAGS] = IPC_ASSET_FIRST | IPC_ASSET_LAST;
        payload[IPC_ASSET_PATHLEN] = (uint8_t)pathlen;
        memcpy(&payload[IPC_ASSET_PATH], path, pathlen);

        for (size_t i = 0; i < datalen; i++) {
            char byte[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
            payload[IPC_ASSET_PATH + pathlen + i] =
                (uint8_t)strtoul(byte, NULL, 16);
        }

        ipcOut("INFO %s, %u bytes\n", path, (unsigned)datalen);
        ipcRequest(IPC_OP_WRITE_ASSET, payload, (uint16_t)total);
    } else if (strcmp(args, "clear") == 0) {
        ipcRequest(IPC_OP_SET_LARGE, NULL, 0);
        ipcRequest(IPC_OP_SET_SMALL, NULL, 0);
    } else if (strcmp(args, "bad") == 0) {
        // The STM32 has no opcode 0x7F. Thus it answers with a NACK.
        ipcRequest(0x7f, NULL, 0);
    } else if (strcmp(args, "badlen") == 0) {
        // The opcode for the RTC needs 4 bytes. Thus this frame gets a NACK.
        ipcRequest(IPC_OP_SET_TIME, "xy", 2);
    } else if (strcmp(args, "reset") == 0) {
        ipcReset();
    } else if (strcmp(args, "noise") == 0) {
        ipcNoise();
    } else if (strncmp(args, "burst ", 6) == 0) {
        ipcBurst((unsigned int)strtoul(args + 6, NULL, 10));
    } else if (strncmp(args, "flood ", 6) == 0) {
        ipcFlood((unsigned int)strtoul(args + 6, NULL, 10));
    } else if (strcmp(args, "stats") == 0) {
        ipcOut("INFO frames=%lu crcerr=%lu badlen=%lu resync=%lu "
                      "dropped=%lu credits=%u\n",
                      (unsigned long)g_parser.stats.frames,
                      (unsigned long)g_parser.stats.crc_errors,
                      (unsigned long)g_parser.stats.bad_length,
                      (unsigned long)g_parser.stats.resyncs,
                      (unsigned long)g_parser.stats.dropped, g_credits);
    } else {
        ipcHelp();
    }
}
