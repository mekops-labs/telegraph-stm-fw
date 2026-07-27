/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_IPC_H
#define __TELEGRAPH_IPC_H

/* Framed binary protocol for the UART between the two MCUs.
 *
 * Note: this library is freestanding C99. It calls no allocator, and it needs
 * no RTOS. Thus the STM32 firmware, the edge MCU and a host test program all
 * compile the same source.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Frame layout
 ****************************************************************************/

/* Every frame has this form. All 16-bit fields are little-endian.
 *
 *   offset  size  field
 *   0       1     SOF, always 0xAA
 *   1       2     LEN, the length of the payload
 *   3       1     OPCODE
 *   4       2     CORRELATION_ID
 *   6       n     PAYLOAD
 *   6+n     2     CRC16
 *
 * Note: LEN counts the payload bytes only. The length of the full frame is
 * thus LEN plus IPC_FRAME_OVERHEAD.
 *
 * Note: the CRC covers the bytes from LEN to the end of the payload. The SOF
 * is a constant, thus it adds no information.
 */

#define IPC_SOF             0xaau
#define IPC_HEADER_LEN      6u
#define IPC_CRC_LEN         2u
#define IPC_FRAME_OVERHEAD  (IPC_HEADER_LEN + IPC_CRC_LEN)

/* The maximum payload. A larger value makes the parser buffer larger.
 *
 * Note: a build overrides this value with -DIPC_MAX_PAYLOAD=n. Both sides of
 * the link must agree, or the receiver rejects the large frames of the sender.
 */

#ifndef IPC_MAX_PAYLOAD
#  define IPC_MAX_PAYLOAD   1024u
#endif

#define IPC_FRAME_MAX       (IPC_MAX_PAYLOAD + IPC_FRAME_OVERHEAD)

/****************************************************************************
 * Correlation IDs
 ****************************************************************************/

/* A request carries a correlation ID. The response repeats it. Thus the
 * broker on the edge MCU sends each response to the correct caller, and it
 * needs no lock across the callers.
 *
 * Note: the value IPC_CORR_ID_PUSH marks a frame that no request asked for.
 * The broker sends these frames to the subscribers of the channel.
 */

#define IPC_CORR_ID_PUSH    0x0000u

/****************************************************************************
 * Opcodes
 ****************************************************************************/

#define IPC_OP_SET_TIME     0x01u  /* edge -> STM32: set the RTC            */
#define IPC_OP_SET_LARGE    0x02u  /* edge -> STM32: text on the main panel */
#define IPC_OP_SET_SMALL    0x03u  /* edge -> STM32: text on the sub panel  */
#define IPC_OP_SET_BRIGHT   0x04u  /* edge -> STM32: the brightness         */
#define IPC_OP_GET_STATE    0x10u  /* edge -> STM32: request the state      */
#define IPC_OP_STATE        0x11u  /* STM32 -> edge: the state              */
#define IPC_OP_LOG          0x12u  /* STM32 -> edge: a log line, a push     */
#define IPC_OP_FLASH        0x20u  /* edge -> STM32: start the flash mode   */
#define IPC_OP_ACK          0xf0u  /* the receiver accepted the frame       */
#define IPC_OP_NACK         0xf1u  /* the receiver rejected the frame       */

/****************************************************************************
 * NACK error codes
 ****************************************************************************/

#define IPC_ERR_NONE          0x00u
#define IPC_ERR_BAD_OPCODE    0x01u  /* the receiver has no such opcode     */
#define IPC_ERR_BAD_LENGTH    0x02u  /* the payload has the wrong length    */
#define IPC_ERR_BAD_PAYLOAD   0x03u  /* a field holds an invalid value      */
#define IPC_ERR_BUSY          0x04u  /* the receiver cannot accept the work */
#define IPC_ERR_FAILED        0x05u  /* the operation started, and it failed */
#define IPC_ERR_UNSUPPORTED   0x06u  /* the build has no support for this   */

/****************************************************************************
 * Return codes
 ****************************************************************************/

/* Note: the library gives its own codes. The values of errno are different
 * between NuttX, ESP-IDF and a host libc.
 */

#define IPC_OK              0
#define IPC_ERR_ARG         (-1)  /* a pointer is NULL, or a value is bad   */
#define IPC_ERR_SPACE       (-2)  /* the destination buffer is too small    */
#define IPC_ERR_TOO_LARGE   (-3)  /* the payload is above IPC_MAX_PAYLOAD   */

/****************************************************************************
 * Payloads
 ****************************************************************************/

#define IPC_PROTO_VERSION   1u

/* The payload of IPC_OP_SET_TIME.
 *
 * The first 4 bytes are a Unix time in seconds, and this value is UTC. The
 * 2 bytes that come after are optional. They give the offset of the local
 * time from UTC, in minutes, with a sign.
 *
 * Note: the RTC keeps UTC only. The offset changes the panels, thus a change
 * of the season needs no change of the RTC.
 */

#define IPC_SET_TIME_LEN     4u
#define IPC_SET_TIME_TZ_LEN  6u

/* The payload of IPC_OP_SET_BRIGHT. One byte sets both devices. Two bytes
 * set the digits and the panels.
 */

#define IPC_BRIGHT_MAX       7u
#define IPC_SET_BRIGHT_LEN   1u
#define IPC_SET_BRIGHT2_LEN  2u

#define IPC_SET_TIME_UTC     0u  /* u32: the Unix time, UTC                */
#define IPC_SET_TIME_OFFSET  4u  /* i16: minutes from UTC, with a sign     */

/* The payload of IPC_OP_STATE. All the multiple-byte fields are
 * little-endian.
 */

#define IPC_STATE_LEN       12u

#define IPC_STATE_TIME      0u   /* u32: the Unix time of the RTC          */
#define IPC_STATE_TEMP      4u   /* i16: tenths of a degree Celsius        */
#define IPC_STATE_FRAMES    6u   /* u16: the count of the accepted frames  */
#define IPC_STATE_CRC_ERR   8u   /* u16: the count of the CRC errors       */
#define IPC_STATE_RESYNC    10u  /* u8:  the count of the resync operations */
#define IPC_STATE_VERSION   11u  /* u8:  IPC_PROTO_VERSION                 */

/****************************************************************************
 * Byte order
 ****************************************************************************/

/* These functions read and write the little-endian fields of a payload. */

static inline uint16_t ipc_get_u16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t ipc_get_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void ipc_put_u16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)(v >> 8);
}

static inline void ipc_put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
  p[2] = (uint8_t)((v >> 16) & 0xff);
  p[3] = (uint8_t)((v >> 24) & 0xff);
}

/****************************************************************************
 * Types
 ****************************************************************************/

/* One frame that the parser accepted.
 *
 * Note: the payload points into the buffer of the parser. The data stays
 * valid only during the callback. A user that keeps the data must copy it.
 */

struct ipc_frame_s
{
  uint8_t        opcode;
  uint16_t       corr_id;
  const uint8_t *payload;
  uint16_t       payload_len;
};

/* Counts of the frames and of the errors on the link. */

struct ipc_stats_s
{
  uint32_t frames;      /* the parser accepted this many frames            */
  uint32_t crc_errors;  /* the CRC of a candidate frame was incorrect      */
  uint32_t bad_length;  /* a LEN field was above IPC_MAX_PAYLOAD           */
  uint32_t resyncs;     /* the parser discarded bytes to find the next SOF */
  uint32_t dropped;     /* the count of the discarded bytes                */
};

/* The state of the parser.
 *
 * Note: this structure holds a full frame. Thus the caller puts it on the
 * heap, and not on the stack of a task.
 */

struct ipc_parser_s
{
  uint8_t            buf[IPC_FRAME_MAX];
  uint16_t           len;
  struct ipc_stats_s stats;
};

/* The parser calls this function one time for each accepted frame. */

typedef void (*ipc_frame_cb_t)(void *arg, const struct ipc_frame_s *frame);

/****************************************************************************
 * Name: ipc_crc16
 *
 * Description:
 *   Calculate the CRC-16/CCITT-FALSE of a buffer. The polynomial is 0x1021,
 *   and the initial value is 0xffff. The function reflects no input and no
 *   output, and it applies no final exclusive-or.
 *
 ****************************************************************************/

uint16_t ipc_crc16(const void *data, size_t len);

/****************************************************************************
 * Name: ipc_encode
 *
 * Description:
 *   Write a full frame into dst.
 *
 * Returned Value:
 *   The length of the frame, or a negative IPC_ERR_* value.
 *
 ****************************************************************************/

int ipc_encode(void *dst, size_t dstlen, uint8_t opcode, uint16_t corr_id,
               const void *payload, uint16_t payload_len);

/****************************************************************************
 * Name: ipc_encode_ack
 *
 * Description:
 *   Write an ACK frame into dst. The payload is the credit count. This value
 *   gives the number of the additional frames that the receiver accepts.
 *
 *   Note: the link has no RTS/CTS signal. These credits are the only flow
 *   control. A sender with zero credits stops until the next ACK.
 *
 * Returned Value:
 *   The length of the frame, or a negative IPC_ERR_* value.
 *
 ****************************************************************************/

int ipc_encode_ack(void *dst, size_t dstlen, uint16_t corr_id,
                   uint8_t credits);

/****************************************************************************
 * Name: ipc_encode_nack
 *
 * Description:
 *   Write a NACK frame into dst. The payload is one IPC_ERR_* code.
 *
 * Returned Value:
 *   The length of the frame, or a negative IPC_ERR_* value.
 *
 ****************************************************************************/

int ipc_encode_nack(void *dst, size_t dstlen, uint16_t corr_id,
                    uint8_t error);

/****************************************************************************
 * Name: ipc_parser_init
 *
 * Description:
 *   Set the parser to the empty state, and set all the counts to zero.
 *
 ****************************************************************************/

void ipc_parser_init(struct ipc_parser_s *parser);

/****************************************************************************
 * Name: ipc_parser_push
 *
 * Description:
 *   Give received bytes to the parser. The bytes come in any quantity, and a
 *   frame divides across more than one call.
 *
 *   For each accepted frame, the parser calls cb one time.
 *
 *   Note: an incorrect CRC, or a LEN above the maximum, makes the parser
 *   discard the first byte. The parser then finds the next SOF and tries
 *   again. Thus a false SOF byte in corrupt data delays the next frame, but
 *   it does not stop the link.
 *
 * Returned Value:
 *   The number of the accepted frames.
 *
 ****************************************************************************/

unsigned int ipc_parser_push(struct ipc_parser_s *parser, const void *data,
                             size_t len, ipc_frame_cb_t cb, void *arg);

/****************************************************************************
 * Name: ipc_parser_timeout
 *
 * Description:
 *   Tell the parser that the receive line is idle.
 *
 *   Corrupt data sometimes gives a false SOF with a LEN that is possible. The
 *   parser then waits for a frame that no sender transmits, and a good frame
 *   behind it stays in the buffer. This function removes that condition: it
 *   takes out the complete frames, and it discards the remainder.
 *
 *   The caller uses the idle detection of the UART. A period of three frame
 *   times without a byte is sufficient.
 *
 * Returned Value:
 *   The number of the accepted frames.
 *
 ****************************************************************************/

unsigned int ipc_parser_timeout(struct ipc_parser_s *parser,
                                ipc_frame_cb_t cb, void *arg);

/****************************************************************************
 * Name: ipc_parser_pending
 *
 * Description:
 *   Give true if the parser holds a partial frame. The caller starts its
 *   idle timer only in this condition.
 *
 ****************************************************************************/

bool ipc_parser_pending(const struct ipc_parser_s *parser);

#ifdef __cplusplus
}
#endif

#endif /* __TELEGRAPH_IPC_H */
