/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <telegraph/ipc.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Remove the first n bytes from the buffer. */

static void ipc_discard(struct ipc_parser_s *parser, uint16_t n) {
    if (n >= parser->len) {
        parser->len = 0;
        return;
    }

    memmove(parser->buf, &parser->buf[n], (size_t)(parser->len - n));
    parser->len = (uint16_t)(parser->len - n);
}

/* Discard the first byte, then discard the bytes before the next SOF.
 *
 * Note: the parser comes here after a bad frame. The first byte is a false
 * SOF, thus the search starts after it.
 */

static void ipc_resync(struct ipc_parser_s *parser) {
    uint16_t i;

    parser->stats.resyncs++;

    for (i = 1; i < parser->len; i++) {
        if (parser->buf[i] == IPC_SOF) {
            parser->stats.dropped += i;
            ipc_discard(parser, i);
            return;
        }
    }

    parser->stats.dropped += parser->len;
    parser->len = 0;
}

/* Take the complete frames out of the buffer. */

static unsigned int ipc_drain(struct ipc_parser_s *parser, ipc_frame_cb_t cb,
                              void *arg) {
    unsigned int accepted = 0;

    for (;;) {
        struct ipc_frame_s frame;
        uint16_t payload_len;
        uint16_t total;
        uint16_t want;
        uint16_t got;

        if (parser->len == 0) {
            break;
        }

        if (parser->buf[0] != IPC_SOF) {
            ipc_resync(parser);
            continue;
        }

        if (parser->len < IPC_HEADER_LEN) {
            break;
        }

        payload_len = (uint16_t)(parser->buf[1] | (parser->buf[2] << 8));

        if (payload_len > IPC_MAX_PAYLOAD) {
            parser->stats.bad_length++;
            ipc_resync(parser);
            continue;
        }

        total = (uint16_t)(payload_len + IPC_FRAME_OVERHEAD);

        if (parser->len < total) {
            break;
        }

        want = ipc_crc16(&parser->buf[1], IPC_HEADER_LEN - 1 + payload_len);
        got = (uint16_t)(parser->buf[IPC_HEADER_LEN + payload_len] |
                         (parser->buf[IPC_HEADER_LEN + payload_len + 1] << 8));

        if (want != got) {
            parser->stats.crc_errors++;
            ipc_resync(parser);
            continue;
        }

        frame.opcode = parser->buf[3];
        frame.corr_id = (uint16_t)(parser->buf[4] | (parser->buf[5] << 8));
        frame.payload = &parser->buf[IPC_HEADER_LEN];
        frame.payload_len = payload_len;

        parser->stats.frames++;
        accepted++;

        if (cb != NULL) {
            cb(arg, &frame);
        }

        ipc_discard(parser, total);
    }

    return accepted;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

unsigned int ipc_parser_timeout(struct ipc_parser_s *parser, ipc_frame_cb_t cb,
                                void *arg) {
    unsigned int accepted = 0;

    if (parser == NULL) {
        return 0;
    }

    /* The line is idle. Thus no more bytes come for the data in the buffer.
     * Each step takes the frames that are complete, then it discards one
     * candidate. The buffer is empty at the end.
     */

    while (parser->len > 0) {
        accepted += ipc_drain(parser, cb, arg);

        if (parser->len > 0) {
            ipc_resync(parser);
        }
    }

    return accepted;
}

bool ipc_parser_pending(const struct ipc_parser_s *parser) {
    return parser != NULL && parser->len > 0;
}

void ipc_parser_init(struct ipc_parser_s *parser) {
    if (parser != NULL) {
        memset(parser, 0, sizeof(*parser));
    }
}

unsigned int ipc_parser_push(struct ipc_parser_s *parser, const void *data,
                             size_t len, ipc_frame_cb_t cb, void *arg) {
    const uint8_t *in = (const uint8_t *)data;
    unsigned int accepted = 0;

    if (parser == NULL || (in == NULL && len > 0)) {
        return 0;
    }

    while (len > 0) {
        size_t space = (size_t)(IPC_FRAME_MAX - parser->len);
        size_t take;

        /* A full buffer without a frame holds a false SOF. The resync step
         * makes space.
         */

        if (space == 0) {
            ipc_resync(parser);
            continue;
        }

        take = (len < space) ? len : space;
        memcpy(&parser->buf[parser->len], in, take);
        parser->len = (uint16_t)(parser->len + take);
        in += take;
        len -= take;

        accepted += ipc_drain(parser, cb, arg);
    }

    return accepted;
}
