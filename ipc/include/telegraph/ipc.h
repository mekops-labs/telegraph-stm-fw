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

#define IPC_SOF 0xaau
#define IPC_HEADER_LEN 6u
#define IPC_CRC_LEN 2u
#define IPC_FRAME_OVERHEAD (IPC_HEADER_LEN + IPC_CRC_LEN)

/* The offset of each header field, matching the table above. LEN and
 * CORR_ID are 2 bytes each, low byte first.
 */

#define IPC_OFF_SOF 0u
#define IPC_OFF_LEN 1u
#define IPC_OFF_OPCODE 3u
#define IPC_OFF_CORR_ID 4u

/* The maximum payload. A larger value makes the parser buffer larger.
 *
 * Note: a build overrides this value with -DIPC_MAX_PAYLOAD=n. Both sides of
 * the link must agree, or the receiver rejects the large frames of the sender.
 */

#ifndef IPC_MAX_PAYLOAD
#define IPC_MAX_PAYLOAD 1024u
#endif

#define IPC_FRAME_MAX (IPC_MAX_PAYLOAD + IPC_FRAME_OVERHEAD)

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

#define IPC_OP_SET_TIME 0x01u    /* edge -> STM32: set the RTC            */
#define IPC_OP_SET_TEXT 0x02u    /* edge -> STM32: text on a panel        */
#define IPC_OP_SET_BRIGHT 0x04u  /* edge -> STM32: the brightness         */
#define IPC_OP_SET_ANIM 0x05u    /* edge -> STM32: animate a rectangle    */
#define IPC_OP_ANIM_STOP 0x07u   /* edge -> STM32: stop an animation      */
#define IPC_OP_SET_PIXELS 0x08u  /* edge -> STM32: pixels on a panel      */
#define IPC_OP_SET_TEMPOFF 0x0au /* edge -> STM32: correct the temperature */
#define IPC_OP_SET_SLEEP 0x0bu   /* edge -> STM32: the period without light */
#define IPC_OP_WRITE_ASSET 0x0cu /* edge -> STM32: a part of a file       */
#define IPC_OP_CLEAR 0x0du       /* edge -> STM32: clear a panel or both  */
#define IPC_OP_ANIM_SPEED 0x0fu  /* edge -> STM32: the rate of a movement */
#define IPC_OP_SET_FONT 0x13u    /* edge -> STM32: take a font from flash */
#define IPC_OP_FS_LIST 0x14u     /* edge -> STM32: list a directory        */
#define IPC_OP_FS_READ 0x15u     /* edge -> STM32: read a part of a file   */
#define IPC_OP_FS_DELETE 0x16u   /* edge -> STM32: remove an entry         */
#define IPC_OP_FS_MKDIR 0x17u    /* edge -> STM32: create a directory      */
#define IPC_OP_GET_STATE 0x10u   /* edge -> STM32: request the state       */
#define IPC_OP_STATE 0x11u       /* STM32 -> edge: the state               */
#define IPC_OP_LOG 0x12u         /* STM32 -> edge: a log line, a push      */
#define IPC_OP_FLASH 0x20u       /* edge -> STM32: start the flash mode    */
#define IPC_OP_USB_LIST 0x30u    /* edge -> STM32: the devices of the port */
#define IPC_OP_USB_DEVS 0x31u    /* STM32 -> edge: those devices           */
#define IPC_OP_USB_WRITE 0x32u   /* edge -> STM32: write a channel         */
#define IPC_OP_USB_DATA 0x33u    /* STM32 -> edge: a channel read, a push  */
#define IPC_OP_USB_SUB 0x34u     /* edge -> STM32: follow a channel        */
#define IPC_OP_ACK 0xf0u         /* the receiver accepted the frame        */

/* One credit gives this many bytes of the receive buffer. A frame thus costs
 * more than one credit if its length is above this value.
 */

#define IPC_CREDIT_UNIT 64u

/* The credits that a frame of this length costs. */

#define IPC_FRAME_CREDITS(len)                                                 \
    (((len) + IPC_FRAME_OVERHEAD + IPC_CREDIT_UNIT - 1) / IPC_CREDIT_UNIT)
#define IPC_OP_NACK 0xf1u /* the receiver rejected the frame       */

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

/* The protocol is experimental. Thus a change that breaks the earlier layout
 * keeps this value, and the two MCUs always come from the same source.
 */

#define IPC_PROTO_VERSION 1u

/* The payload of IPC_OP_SET_LARGE and IPC_OP_SET_SMALL.
 *
 *   [attributes u8] [the text in UTF-8]
 *
 * An empty payload clears the panel. A payload of one byte alone also clears
 * it, because the text is then empty.
 *
 * The bits 0 and 1 of the attributes give the place of the text across the
 * panel. The value 0 puts it in the middle, thus a sender that writes no
 * attribute gets a text in the middle. The other bits are 0.
 */

#define IPC_TEXT_PANEL 0u
#define IPC_TEXT_ATTRS 1u
#define IPC_TEXT_BODY 2u

#define IPC_ALIGN_MASK 0x03u
#define IPC_ALIGN_CENTRE 0u
#define IPC_ALIGN_LEFT 1u
#define IPC_ALIGN_RIGHT 2u

/* The bits 2 and 3 give the place of the text down the panel. A compact font
 * from the flash gives two lines on a panel of 14 rows, thus one text takes
 * the top and another takes the bottom.
 */

#define IPC_VALIGN_SHIFT 2u
#define IPC_VALIGN_MASK 0x0cu
#define IPC_VALIGN_MIDDLE 0u
#define IPC_VALIGN_TOP 1u
#define IPC_VALIGN_BOTTOM 2u

#define IPC_TEXT_ATTR_MASK (IPC_ALIGN_MASK | IPC_VALIGN_MASK)

/* The payload of IPC_OP_SET_TIME.
 *
 * The first 4 bytes are a Unix time in seconds, and this value is UTC. The
 * 2 bytes that come after are optional. They give the offset of the local
 * time from UTC, in minutes, with a sign.
 *
 * Note: the RTC keeps UTC only. The offset changes the panels, thus a change
 * of the season needs no change of the RTC.
 */

#define IPC_SET_TIME_LEN 4u
#define IPC_SET_TIME_TZ_LEN 6u

/* The payload of IPC_OP_SET_BRIGHT. One byte sets both devices. Two bytes
 * set the digits and the panels.
 *
 * The value 0 turns the device off. The values 1 to IPC_BRIGHT_MAX give eight
 * levels of brightness, from the dimmest to the full level.
 */

#define IPC_BRIGHT_MAX 8u
#define IPC_SET_BRIGHT_LEN 1u
#define IPC_SET_BRIGHT2_LEN 2u

/* The payload of IPC_OP_SET_PIX_LARGE and IPC_OP_SET_PIX_SMALL.
 *
 *   [x u8] [y u8] [width u8] [height u8] [the pixels]
 *
 * The pixels go row by row, and each row starts at a byte. Bit 7 of a byte is
 * the pixel at the left. Thus one row takes (width + 7) / 8 bytes.
 *
 * The rectangle changes those pixels alone, thus the rest of the panel keeps
 * its content. A rectangle that goes past an edge loses the part outside.
 *
 * Note: the main panel is 70 by 14 and the sub panel is 21 by 14. A whole
 * main panel thus takes 126 bytes of pixels, far inside one frame.
 */

#define IPC_PIX_PANEL 0u
#define IPC_PIX_X 1u
#define IPC_PIX_Y 2u
#define IPC_PIX_W 3u
#define IPC_PIX_H 4u
#define IPC_PIX_BITS 5u
#define IPC_PIX_HEADER 5u

/* The payload of IPC_OP_ANIM_LARGE and IPC_OP_ANIM_SMALL.
 *
 *   [x u8] [y u8] [w u8] [h u8]
 *   [flags u8] [period u16] [step u8]
 *   [source width u8] [source height u8]
 *   [the source]
 *
 * The board keeps a source larger than the rectangle, and it moves a window
 * over that source. The window moves by "step" pixels every "period"
 * milliseconds, and it returns to the start at the end of the source.
 *
 * A step of one pixel gives a scroll. A step of the width of the rectangle
 * gives the frames of a sprite, because the window then jumps from one frame
 * to the next. Thus one mechanism carries both.
 *
 * The flag IPC_ANIM_VERTICAL moves the window down instead of across.
 *
 * The flag IPC_ANIM_TEXT makes the source a text in UTF-8 instead of pixels.
 * The board then draws that text into the source itself, thus a scrolling
 * message costs one frame and not one frame for each step. The width and the
 * height of the source are 0 in that case, because the board computes them.
 *
 * Note: the source of the main panel holds IPC_ANIM_SRC_MAX bytes, and the
 * sub panel holds half of that. A larger source gets a NACK.
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

/* With IPC_ANIM_FILE the body is the path of a sprite in the flash of the
 * board, and not pixels. That file gives the size of the source and the step,
 * thus the payload carries the rectangle and the period alone.
 *
 * A sprite file starts with a header of 10 bytes:
 *
 *   [magic u32 "TGS1"] [width u16] [height u16] [step u8] [flags u8]
 *
 * The pixels follow, row by row, as in the operations above.
 */

#define IPC_SPRITE_MAGIC 0x31534754u
#define IPC_SPRITE_HEADER 10u

#define IPC_ANIM_SRC_MAX 512u

/* The payload of IPC_OP_ANIM_STOP names a panel, or it is empty for both. The
 * rectangle keeps the pixels of its last step.
 */

/* The payload of IPC_OP_SET_FONT is the name of a font, without a directory
 * and without an ending. The board keeps its fonts in one place and it takes
 * one format, thus a caller needs neither.
 *
 * An empty payload asks for the names that the board holds. The reply carries
 * the same opcode and the same correlation ID, and its payload is those names
 * with a newline after each one.
 *
 * A font carries its own cell. The font of 5 by 7 gives one line on a panel of
 * 14 rows, and a compact font gives two.
 */

/* The same rule holds for a sprite: IPC_OP_SET_ANIM with the flag
 * IPC_ANIM_FILE takes the name of that sprite, and an empty payload asks for
 * the names that the board holds.
 */

#define IPC_LIST_MAX 192u

/* The rate of the link. The STM32 takes its rate from CONFIG_USART1_BAUD, and
 * its server refuses to build if that value differs from this one. Thus the
 * two sides never take different rates.
 */

#define IPC_BAUD 460800u

/* The payload of IPC_OP_ANIM_SPEED.
 *
 *   [panel u8] [period u16] [step u8]
 *
 * The panel is 0 for the main one and 1 for the sub one. The period is the
 * time of one step in milliseconds, and a step of 0 keeps the step that the
 * animation already has.
 *
 * The animation keeps its source and its place, thus the rate changes without
 * the cost of sending that source again.
 *
 * Note: a panel without an animation gets a NACK with the code 0x03.
 */

#define IPC_SPEED_PANEL 0u
#define IPC_SPEED_PERIOD 1u
#define IPC_SPEED_STEP 3u
#define IPC_SPEED_LEN 4u

/* The payload of IPC_OP_CLEAR names the panel, or it is empty.
 *
 *   (empty)              both panels
 *   [IPC_PANEL_MAIN]     the main panel alone
 *   [IPC_PANEL_SUB]      the sub panel alone
 *
 * The panel loses every pixel. An animation of that panel stops as well,
 * because an animation that kept its steps would draw over the panel again at
 * its next one.
 */

/* The payload of IPC_OP_SET_TEMPOFF. The value is a correction in tenths of a
 * degree Celsius, with a sign. The board adds it to each reading.
 */

#define IPC_SET_TEMPOFF_LEN 2u

/* The payload of IPC_OP_SET_SLEEP. The display gives no light between these
 * two minutes of the local day. A start after the end goes through midnight.
 *
 * Note: the value IPC_SLEEP_OFF for the start stops this function.
 *
 * Note: the period does not change the brightness. Thus the display takes its
 * previous levels at the end of the period.
 */

/* The payload of IPC_OP_WRITE_ASSET. The frame carries one part of a file of
 * the assets.
 *
 *   [flags u8] [length of the path u8] [the path] [the data]
 *
 * The first part makes the file empty, and the last part closes it. A file
 * that takes one part alone carries both marks.
 *
 * Note: the board keeps one file open. A first part closes a file that an
 * earlier transfer left open.
 *
 * Note: the path of every part of one file must be the same.
 */

#define IPC_ASSET_FLAGS 0u
#define IPC_ASSET_PATHLEN 1u
#define IPC_ASSET_PATH 2u

#define IPC_ASSET_FIRST 0x01u
#define IPC_ASSET_LAST 0x02u

#define IPC_ASSET_PATH_MAX 64u

/* The storage that the edge MCU reaches. Every path of every storage opcode
 * must start with one of these roots, and a path holding ".." is refused.
 * Thus the settings, the raw devices and the rest of the file tree stay out of
 * reach of the link.
 *
 *   IPC_ROOT_ASSETS  the file system of the flash of the board
 *   IPC_ROOT_MEDIA   the mount of a mass storage device on the USB port
 */

#define IPC_ROOT_ASSETS "/assets"
#define IPC_ROOT_MEDIA "/media"

/* The payload of IPC_OP_FS_LIST names a directory, and the reply carries the
 * same opcode.
 *
 *   request: [index u16] [the path]
 *   reply:   [next index u16] [the entries]
 *
 * Each entry is:
 *
 *   [kind u8] [size u32] [length of the name u8] [the name]
 *
 * The index of the request is the ordinal of the first entry that the reply
 * carries, thus a directory of any length takes as many requests as it needs.
 * The next index of the reply names the first entry that the reply leaves out,
 * and IPC_FS_INDEX_END states that no entry remains.
 *
 * Note: the size of a directory is 0.
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

/* The payload of IPC_OP_FS_READ takes one part of a file, and the reply
 * carries the same opcode.
 *
 *   request: [offset u32] [length u16] [the path]
 *   reply:   [offset u32] [the data]
 *
 * A reply shorter than the requested length holds the end of the file. A
 * reply of the offset alone states that the offset is at or past that end,
 * thus a caller reads until it takes such a reply.
 *
 * Note: a length above IPC_FS_READ_MAX gives that value instead of a NACK.
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

/* IPC_OP_USB_LIST takes no payload, and IPC_OP_USB_DEVS answers it with one
 * record for each device of the USB port:
 *
 *   [channel u8] [kind u8] [length of the name u8] [the name]
 *
 * A serial device carries the number of its channel, which the write and the
 * subscribe opcodes name. A mass storage device carries the channel
 * IPC_USB_NO_CHANNEL, because the storage opcodes reach it by path instead.
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

/* The payload of IPC_OP_USB_WRITE:
 *
 *   [channel u8] [sequence u8] [the data]
 *
 * **A write is the one operation of this protocol that a repeat does not leave
 * unchanged.** Every other opcode sets a state, thus a sender that repeats it
 * after a lost reply causes no harm. A write adds bytes to a stream instead,
 * and a repeat would add them twice.
 *
 * The sequence closes that gap. A sender counts up for each new write, and it
 * keeps the value for every attempt of the same write. The board holds the
 * value of the last write it took, thus it answers a repeat with an ACK and
 * writes nothing. The count wraps at 256, which no repeat of one write can
 * reach.
 *
 * Note: the board forgets that value when a channel opens or closes, thus a
 * sender starts a new channel from any value.
 *
 * IPC_OP_USB_DATA carries a channel and the data of that channel as a push:
 *
 *   [channel u8] [the data]
 *
 * The board follows a channel only after IPC_OP_USB_SUB turns it on:
 *
 *   [channel u8] [state u8]
 *
 * A state of 0 stops the push frames and closes the device. Any other value
 * starts them. The reply of each is an ACK.
 *
 * Note: the board follows one channel at a time.
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

/* The payload of IPC_OP_STATE. All the multiple-byte fields are
 * little-endian.
 *
 * The first IPC_STATE_LEN bytes are always present. The bytes that come after
 * them are the version of the firmware, as text without a terminator. The
 * length of that text is the length of the payload less IPC_STATE_LEN.
 *
 * Note: the version comes from the git tags of the firmware. The edge MCU
 * compares it with the version of the image that it holds. Thus it flashes the
 * board only when the two differ.
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

int ipc_encode_ack(void *dst, size_t dstlen, uint16_t corr_id, uint8_t credits);

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

int ipc_encode_nack(void *dst, size_t dstlen, uint16_t corr_id, uint8_t error);

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
 *   For each accepted frame, the parser calls callback one time.
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
                             size_t len, ipc_frame_cb_t callback, void *arg);

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
                                ipc_frame_cb_t callback, void *arg);

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
