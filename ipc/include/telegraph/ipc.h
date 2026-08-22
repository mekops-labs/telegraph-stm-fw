/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_IPC_H
#define __TELEGRAPH_IPC_H

/* Framed binary protocol for the UART between the two MCUs. Freestanding C99
 * with no allocator and no RTOS, thus both MCUs and a host test compile it.
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

/* [SOF 1] [LEN 2] [OPCODE 1] [CORR_ID 2] [PAYLOAD n] [CRC16 2], the 16-bit
 * fields little-endian. LEN counts the payload alone, and the CRC covers LEN
 * through the end of it.
 */

#define IPC_SOF 0xaau
#define IPC_HEADER_LEN 6u
#define IPC_CRC_LEN 2u
#define IPC_FRAME_OVERHEAD (IPC_HEADER_LEN + IPC_CRC_LEN)

/* The offset of each header field. LEN and CORR_ID are 2 bytes each. */

#define IPC_OFF_SOF 0u
#define IPC_OFF_LEN 1u
#define IPC_OFF_OPCODE 3u
#define IPC_OFF_CORR_ID 4u

/* The maximum payload, and the size of the parser buffer. A build overrides
 * it with -DIPC_MAX_PAYLOAD=n, and both sides of the link must agree.
 */

#ifndef IPC_MAX_PAYLOAD
#define IPC_MAX_PAYLOAD 1024u
#endif

#define IPC_FRAME_MAX (IPC_MAX_PAYLOAD + IPC_FRAME_OVERHEAD)

/****************************************************************************
 * Correlation IDs
 ****************************************************************************/

/* A request carries a correlation ID and the response repeats it, thus the
 * broker routes a reply to its caller without a lock. IPC_CORR_ID_PUSH marks
 * a frame no request asked for.
 */

#define IPC_CORR_ID_PUSH 0x0000u

/****************************************************************************
 * Panels
 ****************************************************************************/

/* Every operation that names a panel takes this value as the first byte of
 * its payload. Thus one opcode serves both panels, and an operation needs no
 * opcode of its own for each of them.
 */

#define IPC_PANEL_MAIN 0u
#define IPC_PANEL_SUB 1u

/****************************************************************************
 * Opcodes
 ****************************************************************************/

/* The opcodes are grouped by what they reach: the board itself, the display,
 * the storage, the USB port. Each group starts on a boundary of 16, thus a new
 * opcode joins its own group and the number says where it belongs.
 */

/* The board: its state, its clock, its mode. */

#define IPC_OP_GET_STATE 0x01u   /* edge -> STM32: request the state       */
#define IPC_OP_STATE 0x02u       /* STM32 -> edge: the state               */
#define IPC_OP_LOG 0x03u         /* STM32 -> edge: a log line, a push      */
#define IPC_OP_SET_TIME 0x04u    /* edge -> STM32: set the RTC             */
#define IPC_OP_SET_TEMPOFF 0x05u /* edge -> STM32: correct the temperature */
#define IPC_OP_FLASH 0x06u       /* edge -> STM32: start the flash mode    */

/* The display: what the panels show, and how they move. */

#define IPC_OP_CLEAR 0x10u      /* edge -> STM32: clear a panel or both   */
#define IPC_OP_SET_BRIGHT 0x11u /* edge -> STM32: the brightness          */
#define IPC_OP_SET_SLEEP 0x12u  /* edge -> STM32: the period without light */
#define IPC_OP_SET_TEXT 0x13u   /* edge -> STM32: text on a panel         */
#define IPC_OP_SET_PIXELS 0x14u /* edge -> STM32: pixels on a panel       */
#define IPC_OP_SET_FONT 0x15u   /* edge -> STM32: take a font from flash  */
#define IPC_OP_SET_ANIM 0x16u   /* edge -> STM32: animate a rectangle     */
#define IPC_OP_ANIM_SPEED 0x17u /* edge -> STM32: the rate of a movement  */
#define IPC_OP_ANIM_STOP 0x18u  /* edge -> STM32: stop an animation       */

/* The storage: the flash of the board and the USB device alike. */

#define IPC_OP_FS_LIST 0x20u   /* edge -> STM32: list a directory        */
#define IPC_OP_FS_READ 0x21u   /* edge -> STM32: read a part of a file   */
#define IPC_OP_FS_WRITE 0x22u  /* edge -> STM32: write a part of a file  */
#define IPC_OP_FS_DELETE 0x23u /* edge -> STM32: remove an entry         */
#define IPC_OP_FS_MKDIR 0x24u  /* edge -> STM32: create a directory      */

/* The USB port: its devices, and the channel of a serial device. */

#define IPC_OP_USB_LIST 0x30u  /* edge -> STM32: the devices of the port */
#define IPC_OP_USB_DEVS 0x31u  /* STM32 -> edge: those devices           */
#define IPC_OP_USB_WRITE 0x32u /* edge -> STM32: write a channel         */
#define IPC_OP_USB_DATA 0x33u  /* STM32 -> edge: a channel read, a push  */
#define IPC_OP_USB_SUB 0x34u   /* edge -> STM32: follow a channel        */

/* The transport: the reply that every request takes. */

#define IPC_OP_ACK 0xf0u  /* the receiver accepted the frame        */
#define IPC_OP_NACK 0xf1u /* the receiver rejected the frame        */

/* One credit gives this many bytes of the receive buffer. A frame thus costs
 * more than one credit if its length is above this value.
 */

#define IPC_CREDIT_UNIT 64u

/* The credits that a frame of this length costs. */

#define IPC_FRAME_CREDITS(len)                                                 \
    (((len) + IPC_FRAME_OVERHEAD + IPC_CREDIT_UNIT - 1) / IPC_CREDIT_UNIT)

/****************************************************************************
 * NACK error codes
 ****************************************************************************/

#define IPC_ERR_NONE 0x00u
#define IPC_ERR_BAD_OPCODE 0x01u  /* the receiver has no such opcode     */
#define IPC_ERR_BAD_LENGTH 0x02u  /* the payload has the wrong length    */
#define IPC_ERR_BAD_PAYLOAD 0x03u /* a field holds an invalid value      */
#define IPC_ERR_BUSY 0x04u        /* the receiver cannot accept the work */
#define IPC_ERR_FAILED 0x05u      /* the operation started, and it failed */
#define IPC_ERR_UNSUPPORTED 0x06u /* the build has no support for this   */

/****************************************************************************
 * Return codes
 ****************************************************************************/

/* Note: the library gives its own codes. The values of errno are different
 * between NuttX, ESP-IDF and a host libc.
 */

#define IPC_OK 0
#define IPC_ERR_ARG (-1)       /* a pointer is NULL, or a value is bad   */
#define IPC_ERR_SPACE (-2)     /* the destination buffer is too small    */
#define IPC_ERR_TOO_LARGE (-3) /* the payload is above IPC_MAX_PAYLOAD   */

/****************************************************************************
 * Payloads
 ****************************************************************************/

/* Experimental: a change that breaks the layout keeps this value, and the two
 * MCUs always come from the same source.
 */

#define IPC_PROTO_VERSION 1u

/* IPC_OP_SET_TEXT: [panel u8] [attributes u8] [the text in UTF-8]. An empty
 * text clears the panel. Bits 0 and 1 place it across the panel, and 0 is the
 * middle.
 */

#define IPC_TEXT_PANEL 0u
#define IPC_TEXT_ATTRS 1u
#define IPC_TEXT_BODY 2u

#define IPC_ALIGN_MASK 0x03u
#define IPC_ALIGN_CENTRE 0u
#define IPC_ALIGN_LEFT 1u
#define IPC_ALIGN_RIGHT 2u

/* Bits 2 and 3 place the text down the panel. A compact font gives two lines
 * on 14 rows, thus one text takes the top and another the bottom.
 */

#define IPC_VALIGN_SHIFT 2u
#define IPC_VALIGN_MASK 0x0cu
#define IPC_VALIGN_MIDDLE 0u
#define IPC_VALIGN_TOP 1u
#define IPC_VALIGN_BOTTOM 2u

#define IPC_TEXT_ATTR_MASK (IPC_ALIGN_MASK | IPC_VALIGN_MASK)

/* IPC_OP_SET_TIME: [epoch u32] [offset i16]. The epoch is UTC and the offset
 * is optional, in minutes. The RTC keeps UTC alone, thus a change of the
 * season changes the panels and not the RTC.
 */

#define IPC_SET_TIME_LEN 4u
#define IPC_SET_TIME_TZ_LEN 6u

/* IPC_OP_SET_BRIGHT: one byte sets both devices, two set the digits and the
 * panels. The value 0 turns a device off, and IPC_BRIGHT_MAX is full.
 */

#define IPC_BRIGHT_MAX 8u
#define IPC_SET_BRIGHT_LEN 1u
#define IPC_SET_BRIGHT2_LEN 2u

/* IPC_OP_SET_PIXELS: [panel u8] [x u8] [y u8] [w u8] [h u8] [the pixels], row
 * by row with bit 7 leftmost, thus a row takes (w + 7) / 8 bytes. The
 * rectangle changes those pixels alone and loses what falls past an edge.
 */

#define IPC_PIX_PANEL 0u
#define IPC_PIX_X 1u
#define IPC_PIX_Y 2u
#define IPC_PIX_W 3u
#define IPC_PIX_H 4u
#define IPC_PIX_BITS 5u
#define IPC_PIX_HEADER 5u

/* IPC_OP_SET_ANIM moves a window over a source larger than the rectangle, by
 * `step` pixels every `period` ms. A step of one pixel scrolls; a step of the
 * width plays the frames of a sprite. Refer to docs/ipc-protocol.md.
 */

#define IPC_ANIM_PANEL 0u
#define IPC_ANIM_X 1u
#define IPC_ANIM_Y 2u
#define IPC_ANIM_W 3u
#define IPC_ANIM_H 4u
#define IPC_ANIM_FLAGS 5u
#define IPC_ANIM_PERIOD 6u
#define IPC_ANIM_STEP 8u
#define IPC_ANIM_SRCW 9u
#define IPC_ANIM_SRCH 10u
#define IPC_ANIM_BODY 11u

#define IPC_ANIM_VERTICAL 0x01u
#define IPC_ANIM_TEXT 0x02u
#define IPC_ANIM_FILE 0x04u
#define IPC_ANIM_FLAG_MASK 0x07u

/* IPC_ANIM_FILE makes the body the path of a sprite in the board's flash. Its
 * header is [magic u32 "TGS1"] [w u16] [h u16] [step u8] [flags u8], then the
 * pixels row by row.
 */

#define IPC_SPRITE_MAGIC 0x31534754u
#define IPC_SPRITE_HEADER 10u

#define IPC_ANIM_SRC_MAX 512u

/* The payload of IPC_OP_ANIM_STOP names a panel, or it is empty for both. The
 * rectangle keeps the pixels of its last step.
 */

/* IPC_OP_SET_FONT takes the name of a font, with no directory and no ending.
 * An empty payload asks for the names the board holds, answered under the
 * same opcode and ID, one name per line.
 */

/* A sprite answers the same way: an empty IPC_ANIM_FILE payload lists them. */

#define IPC_LIST_MAX 192u

/* The rate of the link. The STM32 refuses to build when CONFIG_USART1_BAUD
 * differs from it, thus the two sides never take different rates.
 */

#define IPC_BAUD 460800u

/* IPC_OP_ANIM_SPEED: [panel u8] [period u16] [step u8]. A step of 0 keeps the
 * one the animation has, which keeps its source and its place. A panel with
 * no animation takes a NACK of 0x03.
 */

#define IPC_SPEED_PANEL 0u
#define IPC_SPEED_PERIOD 1u
#define IPC_SPEED_STEP 3u
#define IPC_SPEED_LEN 4u

/* IPC_OP_CLEAR names a panel, or is empty for both. The panel loses every
 * pixel and its animation stops with it.
 */

/* The payload of IPC_OP_SET_TEMPOFF. The value is a correction in tenths of a
 * degree Celsius, with a sign. The board adds it to each reading.
 */

#define IPC_SET_TEMPOFF_LEN 2u

/* IPC_OP_SET_SLEEP: the display gives no light between two minutes of the
 * local day, and a start past the end goes through midnight. A start of
 * IPC_SLEEP_OFF stops it, and the brightness levels are untouched.
 */

/* IPC_OP_FS_WRITE: [flags u8] [path len u8] [the path] [the data], one part
 * of a file. The first part empties the file and the last closes it. The
 * board keeps one file open, and every part of one file names one path.
 */

#define IPC_FS_WRITE_FLAGS 0u
#define IPC_FS_WRITE_PATHLEN 1u
#define IPC_FS_WRITE_PATH 2u

#define IPC_FS_WRITE_FIRST 0x01u
#define IPC_FS_WRITE_LAST 0x02u

#define IPC_FS_PATH_MAX 64u

/* Every path of a storage opcode starts with one of these roots, and a path
 * holding ".." is refused, thus the rest of the file tree stays out of reach.
 */

#define IPC_ROOT_ASSETS "/assets"
#define IPC_ROOT_MEDIA "/media"

/* IPC_OP_FS_LIST: request [index u16] [the path], reply [next index u16] and
 * the entries. The index is the ordinal of the first entry a reply carries,
 * thus a long directory takes several; IPC_FS_INDEX_END ends it.
 */

#define IPC_FS_LIST_INDEX 0u
#define IPC_FS_LIST_PATH 2u

#define IPC_FS_ENTRY_KIND 0u
#define IPC_FS_ENTRY_SIZE 1u
#define IPC_FS_ENTRY_NAMELEN 5u
#define IPC_FS_ENTRY_NAME 6u

#define IPC_FS_KIND_FILE 0x00u
#define IPC_FS_KIND_DIR 0x01u

#define IPC_FS_INDEX_END 0xffffu

/* IPC_OP_FS_READ: request [offset u32] [length u16] [the path], reply
 * [offset u32] [the data]. A short reply holds the end of the file, and one
 * of the offset alone states the offset is past it.
 */

#define IPC_FS_READ_OFFSET 0u
#define IPC_FS_READ_LENGTH 4u
#define IPC_FS_READ_PATH 6u
#define IPC_FS_READ_DATA 4u /* in the reply, which carries no length   */

#define IPC_FS_READ_MAX 512u

/* The largest reply of IPC_OP_FS_LIST and of IPC_OP_FS_READ. A list longer
 * than this value takes a further request, and a read gives this many bytes at
 * most.
 */

#define IPC_FS_REPLY_MAX (IPC_FS_READ_DATA + IPC_FS_READ_MAX)

/* The payload of IPC_OP_FS_DELETE and of IPC_OP_FS_MKDIR is the path alone.
 * Both answer with an ACK or a NACK.
 *
 * Note: IPC_OP_FS_DELETE takes a file, or a directory that holds no entry.
 */

/* IPC_OP_USB_DEVS answers IPC_OP_USB_LIST with [channel u8] [kind u8]
 * [name len u8] [the name] per device. A mass storage device carries
 * IPC_USB_NO_CHANNEL, since the storage opcodes reach it by path.
 */

#define IPC_USB_DEV_CHANNEL 0u
#define IPC_USB_DEV_KIND 1u
#define IPC_USB_DEV_NAMELEN 2u
#define IPC_USB_DEV_NAME 3u

#define IPC_USB_KIND_SERIAL 0x00u
#define IPC_USB_KIND_STORAGE 0x01u

#define IPC_USB_NO_CHANNEL 0xffu

/* The channels that the board looks for. One device sits on the port, thus a
 * larger count needs a hub.
 */

#define IPC_USB_CHANNELS 2u

#define IPC_USB_CHANNELS 2u

/* IPC_OP_USB_WRITE: [channel u8] [sequence u8] [the data]. A write is the one
 * operation a repeat does not leave unchanged, thus the sequence marks a
 * retry: the board ACKs a repeat of the last value and writes nothing.
 */

/* IPC_OP_USB_DATA pushes [channel u8] [the data]. IPC_OP_USB_SUB takes
 * [channel u8] [state u8] and turns that push on, one channel at a time; a
 * state of 0 stops it and closes the device.
 */

#define IPC_USB_CHANNEL 0u

#define IPC_USB_WRITE_SEQ 1u
#define IPC_USB_WRITE_DATA 2u

#define IPC_USB_PUSH_DATA 1u

#define IPC_USB_STATE 1u
#define IPC_USB_SUB_LEN 2u

/* One push frame carries this many bytes of a channel at most. */

#define IPC_USB_READ_MAX 256u

/* The largest reply that the board builds outside the frame buffer: a list of
 * a directory, a part of a file, the devices of the USB port.
 */

#define IPC_REPLY_MAX IPC_FS_REPLY_MAX

#define IPC_SET_SLEEP_LEN 4u
#define IPC_SLEEP_START 0u /* u16: the minute that stops the light   */
#define IPC_SLEEP_END 2u   /* u16: the minute that starts it again   */
#define IPC_SLEEP_OFF 0xffffu
#define IPC_MINUTES_PER_DAY 1440u

#define IPC_SET_TIME_UTC 0u    /* u32: the Unix time, UTC                */
#define IPC_SET_TIME_OFFSET 4u /* i16: minutes from UTC, with a sign     */

/* IPC_OP_STATE: the first IPC_STATE_LEN bytes always, then the version of the
 * firmware as text with no terminator, of the payload length less that. The
 * edge MCU compares it with the image it holds before writing the flash.
 */

#define IPC_STATE_LEN 12u

#define IPC_STATE_TIME 0u     /* u32: the Unix time of the RTC          */
#define IPC_STATE_TEMP 4u     /* i16: tenths of a degree Celsius        */
#define IPC_STATE_FRAMES 6u   /* u16: the count of the accepted frames  */
#define IPC_STATE_CRC_ERR 8u  /* u16: the count of the CRC errors       */
#define IPC_STATE_RESYNC 10u  /* u8:  the count of the resync operations */
#define IPC_STATE_VERSION 11u /* u8:  IPC_PROTO_VERSION                 */
#define IPC_STATE_FWVER 12u   /* text: the version of the firmware      */

/* The longest version text that a STATE frame carries. */

#define IPC_FWVER_MAX 32u

/****************************************************************************
 * Byte order
 ****************************************************************************/

/* A byte holds this many bits, and this masks one out of a wider value. */

#define IPC_BYTE_BITS 8u
#define IPC_BYTE_MASK 0xffu

/* These functions read and write the little-endian fields of a payload. */

static inline uint16_t ipc_get_u16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | (bytes[1] << IPC_BYTE_BITS));
}

static inline uint32_t ipc_get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << IPC_BYTE_BITS) |
           ((uint32_t)bytes[2] << (2 * IPC_BYTE_BITS)) |
           ((uint32_t)bytes[3] << (3 * IPC_BYTE_BITS));
}

static inline void ipc_put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & IPC_BYTE_MASK);
    bytes[1] = (uint8_t)(value >> IPC_BYTE_BITS);
}

static inline void ipc_put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & IPC_BYTE_MASK);
    bytes[1] = (uint8_t)((value >> IPC_BYTE_BITS) & IPC_BYTE_MASK);
    bytes[2] = (uint8_t)((value >> (2 * IPC_BYTE_BITS)) & IPC_BYTE_MASK);
    bytes[3] = (uint8_t)((value >> (3 * IPC_BYTE_BITS)) & IPC_BYTE_MASK);
}

/****************************************************************************
 * Types
 ****************************************************************************/

/* One frame that the parser accepted.
 *
 * Note: the payload points into the buffer of the parser. The data stays
 * valid only during the callback. A user that keeps the data must copy it.
 */

struct ipc_frame_s {
    uint8_t opcode;
    uint16_t corr_id;
    const uint8_t *payload;
    uint16_t payload_len;
};

/* Counts of the frames and of the errors on the link. */

struct ipc_stats_s {
    uint32_t frames;     /* the parser accepted this many frames            */
    uint32_t crc_errors; /* the CRC of a candidate frame was incorrect      */
    uint32_t bad_length; /* a LEN field was above IPC_MAX_PAYLOAD           */
    uint32_t resyncs;    /* the parser discarded bytes to find the next SOF */
    uint32_t dropped;    /* the count of the discarded bytes                */
};

/* The state of the parser.
 *
 * Note: this structure holds a full frame. Thus the caller puts it on the
 * heap, and not on the stack of a task.
 */

struct ipc_parser_s {
    uint8_t buf[IPC_FRAME_MAX];
    uint16_t len;
    struct ipc_stats_s stats;
};

/* The parser calls this function one time for each accepted frame. */

typedef void (*ipc_frame_cb_t)(void *arg, const struct ipc_frame_s *frame);

/* The CRC-16/CCITT-FALSE of a buffer: poly 0x1021, init 0xffff, no
 * reflection and no final exclusive-or.
 */

uint16_t ipc_crc16(const void *data, size_t len);

/* Write a full frame into dst. Returns its length, or a negative IPC_ERR_*. */

int ipc_encode(void *dst, size_t dstlen, uint8_t opcode, uint16_t corr_id,
               const void *payload, uint16_t payload_len);

/* Write an ACK into dst, its payload the credits the receiver has left. The
 * link has no RTS/CTS, thus these are the only flow control.
 */

int ipc_encode_ack(void *dst, size_t dstlen, uint16_t corr_id, uint8_t credits);

/* Write a NACK into dst, its payload one IPC_ERR_* code. */

int ipc_encode_nack(void *dst, size_t dstlen, uint16_t corr_id, uint8_t error);

/* Set the parser to the empty state, and every count to zero. */

void ipc_parser_init(struct ipc_parser_s *parser);

/* Give received bytes to the parser, in any quantity, and take a callback per
 * accepted frame. Returns how many were accepted; a bad CRC or length drops
 * one byte and hunts for the next SOF.
 */

unsigned int ipc_parser_push(struct ipc_parser_s *parser, const void *data,
                             size_t len, ipc_frame_cb_t callback, void *arg);

/* Tell the parser the receive line is idle: it takes out what is complete and
 * discards the rest. Without it a false SOF holds a good frame behind it
 * forever. Returns how many frames were accepted.
 */

unsigned int ipc_parser_timeout(struct ipc_parser_s *parser,
                                ipc_frame_cb_t callback, void *arg);

/* True while the parser holds a partial frame, which is when a caller arms
 * its idle timer.
 */

bool ipc_parser_pending(const struct ipc_parser_s *parser);

#ifdef __cplusplus
}
#endif

#endif /* __TELEGRAPH_IPC_H */
