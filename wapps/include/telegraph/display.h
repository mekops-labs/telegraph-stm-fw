/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_DISPLAY_H
#define __TELEGRAPH_DISPLAY_H

/* The request set of tg-display, for an adapter of an outside protocol. The
 * adapter reaches neither the broker nor the STM32.
 */

#include <telegraph/ipc.h>

/****************************************************************************
 * The pipes
 ****************************************************************************/

/* Two pipes per client, as the broker does. The prefix differs from the
 * broker's, thus a client and a peer never take the same pipe.
 */

#define TG_DSP_PIPE_REQ "/dev/pipe/tgd-%s-req"
#define TG_DSP_PIPE_RSP "/dev/pipe/tgd-%s-rsp"

/* The budget of the display alone. The pipe table of the engine is shared. */

#define TG_DSP_MAX_CLIENTS 4

/* The longest name of a client, without the terminator. */

#define TG_DSP_NAME_MAX 15

/****************************************************************************
 * The opcodes
 ****************************************************************************/

/* The framing is the one of telegraph/ipc.h and the display repeats the ID of
 * the request, sending no frame a request did not ask for. These opcodes take
 * a group of their own, thus a crossed pipe gives IPC_ERR_BAD_OPCODE.
 */

#define TG_DSP_OP_GET_STATE 0x40u /* client -> display: request the state  */
#define TG_DSP_OP_STATE 0x41u     /* display -> client: the state          */
#define TG_DSP_OP_TEXT 0x42u      /* client -> display: text on a panel    */
#define TG_DSP_OP_SCROLL 0x43u    /* client -> display: text that moves    */
#define TG_DSP_OP_BRIGHT 0x44u    /* client -> display: the brightness     */
#define TG_DSP_OP_TIME 0x45u      /* client -> display: the clock          */
#define TG_DSP_OP_CLEAR 0x46u     /* client -> display: clear the panels   */
#define TG_DSP_OP_SLEEP 0x47u     /* client -> display: no light in a window */

/* A request takes IPC_OP_ACK, or IPC_OP_NACK with one byte of IPC_ERR_*.
 * IPC_ERR_BUSY means the board gave no reply, thus it says nothing about the
 * request itself.
 */

/****************************************************************************
 * The panels
 ****************************************************************************/

/* The first byte of every request that names a panel. */

#define TG_DSP_PANEL_MAIN 0u
#define TG_DSP_PANEL_SUB 1u

/****************************************************************************
 * The payloads
 ****************************************************************************/

/* TG_DSP_OP_TEXT: [panel u8] [attributes u8] [the text in UTF-8].
 *
 * An empty text clears the panel. The attributes are IPC_ALIGN_* and
 * IPC_VALIGN_*, and 0 puts the text in the middle.
 */

#define TG_DSP_TEXT_PANEL 0u
#define TG_DSP_TEXT_ATTRS 1u
#define TG_DSP_TEXT_BODY 2u

/* TG_DSP_OP_SCROLL: [panel u8] [period u16] [step u8] [the text in UTF-8].
 *
 * The period is milliseconds between two steps and the step is pixels. Either
 * at 0 takes the default below.
 */

#define TG_DSP_SCROLL_PANEL 0u
#define TG_DSP_SCROLL_PERIOD 1u
#define TG_DSP_SCROLL_STEP 3u
#define TG_DSP_SCROLL_BODY 4u

#define TG_DSP_SCROLL_PERIOD_DEFAULT 60u
#define TG_DSP_SCROLL_STEP_DEFAULT 1u

/* TG_DSP_OP_BRIGHT: [digits u8] [panels u8]. One byte sets both devices.
 * The value 0 turns a device off, and IPC_BRIGHT_MAX is the full level.
 */

#define TG_DSP_BRIGHT_DIGITS 0u
#define TG_DSP_BRIGHT_PANELS 1u

#define TG_DSP_BRIGHT_LEN 1u
#define TG_DSP_BRIGHT2_LEN 2u

/* TG_DSP_OP_TIME: [epoch u32] [offset i16]. The epoch is UTC, and 0 takes the
 * time of the engine. The offset is the local time in minutes, and optional.
 */

#define TG_DSP_TIME_EPOCH 0u
#define TG_DSP_TIME_OFFSET 4u

#define TG_DSP_TIME_LEN 4u
#define TG_DSP_TIME_TZ_LEN 6u

/* TG_DSP_OP_SLEEP: [start u16] [end u16], minutes of the local day. Equal
 * values disable the period, and the wire layout matches IPC_OP_SET_SLEEP.
 */

#define TG_DSP_SLEEP_START 0u
#define TG_DSP_SLEEP_END 2u

#define TG_DSP_SLEEP_LEN 4u

/* TG_DSP_OP_CLEAR takes no payload. */

/* TG_DSP_OP_STATE carries the payload of IPC_OP_STATE, forwarded whole. */

/****************************************************************************
 * The client
 ****************************************************************************/

/* The source is wapps/lib/dspclient.c, named in EXTRA_SRCS by the adapter. */

/* The state is the longest reply: the fixed fields, then the version. */

#define TG_DSP_REPLY_MAX 128u

struct tg_dsp_client_s {
    int req_fd;
    int rsp_fd;
    uint16_t corr;
    uint16_t inflight;
    struct ipc_parser_s parser;
    uint8_t frame[IPC_FRAME_MAX];

    /* The reply of the last request. */

    uint8_t reply_op;
    uint8_t reply[TG_DSP_REPLY_MAX];
    unsigned int reply_len;
    bool got_reply;
};

/* A NACK is a reply, thus it is its own value and not a failed transport. */

#define TG_DSP_OK 0
#define TG_DSP_NO_REPLY (-1)
#define TG_DSP_NACK (-2)

/* Open the pipe pair, waiting up to timeout_ms for the display to create it.
 * Returns 0, or -1 when either pipe stayed closed.
 */

int tg_dsp_open(struct tg_dsp_client_s *client, const char *name,
                unsigned int timeout_ms);

/* One request. The reply lands in client->reply, its opcode in reply_op. */

int tg_dsp_ask(struct tg_dsp_client_s *client, uint8_t opcode,
               const void *payload, uint16_t len);

#endif /* __TELEGRAPH_DISPLAY_H */
