/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_DISPLAY_H
#define __TELEGRAPH_DISPLAY_H

/* The interface between the display and the adapters of a protocol.
 *
 * Note: the wapp tg-display owns what the board shows — the composition, the
 * panels, the digits and the clock. A wapp that speaks a protocol of the
 * outside world (HTTP, MQTT) translates that protocol into the requests here,
 * and it reaches neither the broker nor the STM32.
 */

#include <telegraph/ipc.h>

/****************************************************************************
 * The pipes
 ****************************************************************************/

/* The display serves the convention of the broker: a client takes two named
 * pipes, writes its requests to the first one and reads its replies from the
 * second one. The names carry the name of that client.
 *
 * Note: the prefix differs from the one of the broker, thus a client of the
 * display and a peer of the broker never take the same pipe.
 */

#define TG_DSP_PIPE_REQ "/dev/pipe/tgd-%s-req"
#define TG_DSP_PIPE_RSP "/dev/pipe/tgd-%s-rsp"

/* The clients of the display. The engine holds one table of named pipes for
 * every wapp, thus this count is a budget of the display alone and not a
 * division of that table.
 */

#define TG_DSP_MAX_CLIENTS 4

/* The longest name of a client, without the terminator. */

#define TG_DSP_NAME_MAX 15

/****************************************************************************
 * The frames
 ****************************************************************************/

/* A request carries the framing of telegraph/ipc.h — the same encoder, the
 * same parser and the same CRC. Thus this interface adds no second framing,
 * and a client compiles the sources the rest of the board already compiles.
 *
 * A client chooses the correlation ID of its request freely, and the display
 * repeats that ID in the reply. The display sends no frame that a request did
 * not ask for, thus a client subscribes to nothing.
 *
 * Note: the opcodes here are of the display, not of the STM32. They occupy a
 * group of their own in the numbering of telegraph/ipc.h, thus a frame that
 * reaches the wrong reader takes IPC_ERR_BAD_OPCODE instead of an operation
 * the sender did not mean.
 */

#define TG_DSP_OP_GET_STATE 0x40u /* client -> display: request the state  */
#define TG_DSP_OP_STATE 0x41u     /* display -> client: the state          */
#define TG_DSP_OP_TEXT 0x42u      /* client -> display: text on a panel    */
#define TG_DSP_OP_SCROLL 0x43u    /* client -> display: text that moves    */
#define TG_DSP_OP_BRIGHT 0x44u    /* client -> display: the brightness     */
#define TG_DSP_OP_TIME 0x45u      /* client -> display: the clock          */
#define TG_DSP_OP_CLEAR 0x46u     /* client -> display: clear the panels   */

/* Every request that carries no answer of its own takes IPC_OP_ACK, and a
 * request the display or the board refused takes IPC_OP_NACK with one byte of
 * IPC_ERR_*. A client thus handles a refusal of the display and a refusal of
 * the board the same way.
 *
 * The code IPC_ERR_BUSY means the board gave no reply inside the timeout of
 * the display. It is the one code that says nothing about the request itself.
 */

/****************************************************************************
 * The panels
 ****************************************************************************/

/* Every request that names a panel takes this value as the first byte of its
 * payload. The panels are the ones of the board, under names of this
 * interface: a client of the display names no opcode and no panel of the
 * link.
 */

#define TG_DSP_PANEL_MAIN 0u
#define TG_DSP_PANEL_SUB 1u

/****************************************************************************
 * The payloads
 ****************************************************************************/

/* The payload of TG_DSP_OP_TEXT.
 *
 *   [panel u8] [attributes u8] [the text in UTF-8]
 *
 * An empty text clears that panel. The attributes are the alignment bits of
 * telegraph/ipc.h (IPC_ALIGN_*, IPC_VALIGN_*), and a client that writes 0
 * gets a text in the middle.
 *
 * Note: the panel holds a source of IPC_ANIM_SRC_MAX bytes, and the sub panel
 * holds half of that. A longer text takes IPC_ERR_BAD_LENGTH.
 */

#define TG_DSP_TEXT_PANEL 0u
#define TG_DSP_TEXT_ATTRS 1u
#define TG_DSP_TEXT_BODY 2u

/* The payload of TG_DSP_OP_SCROLL.
 *
 *   [panel u8] [period u16] [step u8] [the text in UTF-8]
 *
 * The period is the milliseconds between two steps, and the step is the
 * pixels of one of them. Both are 0 to take the defaults below.
 *
 * Note: a text longer than its panel moves by itself under TG_DSP_OP_TEXT.
 * This request exists to set the rate of that movement.
 */

#define TG_DSP_SCROLL_PANEL 0u
#define TG_DSP_SCROLL_PERIOD 1u
#define TG_DSP_SCROLL_STEP 3u
#define TG_DSP_SCROLL_BODY 4u

#define TG_DSP_SCROLL_PERIOD_DEFAULT 60u
#define TG_DSP_SCROLL_STEP_DEFAULT 1u

/* The payload of TG_DSP_OP_BRIGHT.
 *
 *   [digits u8] [panels u8]
 *
 * The value 0 turns a device off, and IPC_BRIGHT_MAX is the full level. One
 * byte alone sets both devices to the same level.
 */

#define TG_DSP_BRIGHT_DIGITS 0u
#define TG_DSP_BRIGHT_PANELS 1u

#define TG_DSP_BRIGHT_LEN 1u
#define TG_DSP_BRIGHT2_LEN 2u

/* The payload of TG_DSP_OP_TIME.
 *
 *   [epoch u32] [offset i16]
 *
 * The epoch is a Unix time in seconds, and it is UTC. The offset is the local
 * time from UTC in minutes, with a sign, and it is optional. An epoch of 0
 * makes the display take the time of the engine.
 *
 * Note: the board keeps UTC in its RTC and holds the offset in its RAM alone,
 * thus a client sets the offset again after the board resets.
 */

#define TG_DSP_TIME_EPOCH 0u
#define TG_DSP_TIME_OFFSET 4u

#define TG_DSP_TIME_LEN 4u
#define TG_DSP_TIME_TZ_LEN 6u

/* TG_DSP_OP_CLEAR takes no payload, and both panels lose their pixels. */

/* The payload of TG_DSP_OP_STATE is the state of the board, in the layout of
 * IPC_OP_STATE: the first IPC_STATE_LEN bytes are fixed, and the bytes after
 * them are the version of the firmware as text without a terminator.
 *
 * The display forwards this payload as the board gave it. Thus a client reads
 * the fields at IPC_STATE_TIME, IPC_STATE_TEMP, IPC_STATE_FRAMES,
 * IPC_STATE_CRC_ERR, IPC_STATE_RESYNC and IPC_STATE_FWVER.
 */

#endif /* __TELEGRAPH_DISPLAY_H */
