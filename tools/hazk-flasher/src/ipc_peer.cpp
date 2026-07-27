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

// A request gets no more time than this before the peer reports a timeout.
#define IPC_REPLY_MS 500

static struct ipc_parser_s g_parser;
static uint8_t g_tx[IPC_FRAME_MAX];
static bool g_active = false;

// The peer gives each request its own ID. The value 0 marks a push frame,
// thus the counter starts at 1.
static uint16_t g_nextCorrId = 1;

// These values hold the result of the last request.
static uint16_t g_waitCorrId = 0;
static bool g_replied = false;

// The last credit count of an ACK. A count of zero stops the sender.
static uint8_t g_credits = 0xff;

static unsigned long g_lastByteMs = 0;

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
            ipcOut("ACK  id=%u credits=%u\n", frame->corr_id,
                          g_credits);
            break;

        case IPC_OP_NACK:
            ipcOut("NACK id=%u error=0x%02X\n", frame->corr_id,
                          frame->payload_len ? frame->payload[0] : 0);
            break;

        case IPC_OP_STATE:
            if (frame->payload_len != IPC_STATE_LEN) {
                ipcOut("ERR state length %u\n", frame->payload_len);
                break;
            }
            ipcOut(
                "STATE id=%u time=%lu temp=%d.%dC frames=%u crcerr=%u "
                "resync=%u ver=%u\n",
                frame->corr_id,
                (unsigned long)ipc_get_u32(&frame->payload[IPC_STATE_TIME]),
                (int)((int16_t)ipc_get_u16(&frame->payload[IPC_STATE_TEMP])) / 10,
                abs((int)((int16_t)ipc_get_u16(&frame->payload[IPC_STATE_TEMP])) % 10),
                ipc_get_u16(&frame->payload[IPC_STATE_FRAMES]),
                ipc_get_u16(&frame->payload[IPC_STATE_CRC_ERR]),
                frame->payload[IPC_STATE_RESYNC],
                frame->payload[IPC_STATE_VERSION]);
            break;

        default:
            ipcOut("RX   op=0x%02X id=%u len=%u\n", frame->opcode,
                          frame->corr_id, frame->payload_len);
            break;
    }
}

// Transmit a frame, then wait for the reply of that correlation ID.
static void ipcRequest(uint8_t opcode, const void *payload, uint16_t len) {
    if (!g_active) {
        ipcOut("ERR ipc is off\n");
        return;
    }

    if (g_credits == 0) {
        // The receiver has no space. Refer to the flow control of the
        // protocol.
        ipcOut("ERR no credits, wait for an ACK\n");
        return;
    }

    uint16_t id = ipcNextCorrId();
    int n = ipc_encode(g_tx, sizeof(g_tx), opcode, id, payload, len);

    if (n < 0) {
        ipcOut("ERR encode %d\n", n);
        return;
    }

    g_waitCorrId = id;
    g_replied = false;
    Serial1.write(g_tx, (size_t)n);

    unsigned long deadline = millis() + IPC_REPLY_MS;
    while (!g_replied && (long)(millis() - deadline) < 0) {
        ipcPoll();
    }

    if (!g_replied) ipcOut("ERR no reply for id=%u\n", id);
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

// Transmit more than one frame without a wait for the ACK.
//
// Note: this test shows the credits under a burst. The count falls while the
// receive buffer fills. The opcode sets the text of the sub panel, because
// only an ACK carries the credits.
static void ipcBurst(unsigned int count) {
    for (unsigned int i = 0; i < count; i++) {
        uint16_t id = ipcNextCorrId();
        char text[4];
        int n;

        snprintf(text, sizeof(text), "%03u", i % 1000);
        n = ipc_encode(g_tx, sizeof(g_tx), IPC_OP_SET_SMALL, id, text, 3);

        if (n > 0) Serial1.write(g_tx, (size_t)n);
    }

    ipcOut("INFO sent %u requests without a wait\n", count);

    unsigned long deadline = millis() + IPC_REPLY_MS;
    while ((long)(millis() - deadline) < 0) ipcPoll();
}

// Reset the STM32, and keep the peer active.
//
// Note: the STM32 transmits a log frame at its start. Thus this command shows
// a push frame.
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
    g_credits = 0xff;
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
        ipcRequest(IPC_OP_SET_LARGE, args + 6, (uint16_t)strlen(args + 6));
    } else if (strncmp(args, "small ", 6) == 0) {
        ipcRequest(IPC_OP_SET_SMALL, args + 6, (uint16_t)strlen(args + 6));
    } else if (strncmp(args, "bright ", 7) == 0) {
        // The form is "bright <digits> [panels]". Permitted values are 0 to 7.
        uint8_t payload[IPC_SET_BRIGHT2_LEN];
        char *end = NULL;
        uint16_t len = IPC_SET_BRIGHT_LEN;

        payload[0] = (uint8_t)strtoul(args + 7, &end, 10);

        if (end != NULL && *end != '\0') {
            payload[1] = (uint8_t)strtoul(end, NULL, 10);
            len = IPC_SET_BRIGHT2_LEN;
        }

        ipcRequest(IPC_OP_SET_BRIGHT, payload, len);
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
