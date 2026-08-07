/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_BROKER_H
#define __TELEGRAPH_BROKER_H

/* The interface between the broker and its peers.
 *
 * Note: the broker holds the UART grant, thus every other wapp reaches the
 * STM32 through it. A peer carries the frames of telegraph/ipc.h over two
 * named pipes, and the broker gives each frame a correlation ID of the link.
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

/* The engine holds eight named pipes, thus four peers take all of them. */

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

/* The payload of TG_BRK_OP_RAW gives the line settings of raw mode.
 *
 *   [baud u32] [databits u8] [parity u8] [stopbits u8]
 *
 * The parity is one of the characters N, E or O. An empty payload leaves raw
 * mode, and the link returns to IPC_BAUD and 8N1.
 *
 * Note: a peer in raw mode holds the whole link. A request of another peer
 * takes a NACK with IPC_ERR_BUSY until that peer leaves this mode.
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

/* A peer chooses the correlation ID of its request freely. The broker keeps
 * the ID of that peer, gives the frame an ID of its own on the link, and puts
 * the ID of the peer back into the reply. Thus two peers never collide, and
 * neither of them coordinates with the other.
 *
 * Note: the value IPC_CORR_ID_PUSH marks a frame that no request asked for.
 * The broker sends such a frame to the peers that follow its opcode, and it
 * keeps the ID at that value.
 */

#endif /* __TELEGRAPH_BROKER_H */
