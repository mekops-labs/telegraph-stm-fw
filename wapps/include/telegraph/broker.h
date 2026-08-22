/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_BROKER_H
#define __TELEGRAPH_BROKER_H

/* The interface between the broker and its peers. The broker holds the UART
 * grant, thus every other wapp reaches the STM32 through it, over two named
 * pipes carrying the frames of telegraph/ipc.h.
 */

#include <telegraph/ipc.h>

/****************************************************************************
 * The pipes
 ****************************************************************************/

/* Each peer takes two pipes, and their names carry the name of that peer. A
 * peer writes its requests to the first one and reads its replies from the
 * second one.
 */

#define TG_BRK_PIPE_REQ "/dev/pipe/tg-%s-req"
#define TG_BRK_PIPE_RSP "/dev/pipe/tg-%s-rsp"

/* The peers of the broker. This is the budget of the broker alone: another
 * wapp of the board serves peers of its own over the same engine-wide pipe
 * table, thus the count here does not divide that table.
 */

#define TG_BRK_MAX_PEERS 4

/* The longest name of a peer, without the terminator. */

#define TG_BRK_NAME_MAX 15

/****************************************************************************
 * The opcodes of the broker
 ****************************************************************************/

/* These opcodes address the broker itself. The link never carries one of
 * them, thus a peer needs no second framing to reach the broker.
 */

#define TG_BRK_OP_RAW 0xe0u      /* peer -> broker: take or leave raw mode */
#define TG_BRK_OP_RAW_DATA 0xe1u /* both ways: the bytes of raw mode       */

/* TG_BRK_OP_RAW: [baud u32] [databits u8] [parity u8] [stopbits u8].
 *
 * The parity is N, E or O, and an empty payload leaves raw mode. A peer in it
 * holds the whole link, thus another peer takes IPC_ERR_BUSY meanwhile.
 */

#define TG_BRK_RAW_BAUD 0u
#define TG_BRK_RAW_DATABITS 4u
#define TG_BRK_RAW_PARITY 5u
#define TG_BRK_RAW_STOPBITS 6u
#define TG_BRK_RAW_LEN 7u

/* The bytes of raw mode travel in the payload of TG_BRK_OP_RAW_DATA, in both
 * directions. The broker writes the payload to the line without a change, and
 * it carries the bytes of the line back the same way.
 */

/****************************************************************************
 * The correlation IDs
 ****************************************************************************/

/* A peer chooses the ID of its request freely: the broker gives the frame an
 * ID of its own on the link and puts the peer's back into the reply, thus two
 * peers never collide. IPC_CORR_ID_PUSH marks a frame no request asked for.
 */

#endif /* __TELEGRAPH_BROKER_H */
