/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The extended font. The board reads it from the flash, and it holds the
 * letters that the ASCII table does not carry.
 *
 * Note: the font of 5x7 in the firmware stays the fallback. A board with an
 * empty flash thus keeps a legible display.
 *
 * The cell has 10 rows for a letter of 7 rows. Two rows above the letter carry
 * a mark such as an acute, and one row below carries a mark such as an
 * ogonek. Thus a letter keeps its full height, and a mark needs no space
 * inside the letter.
 */

#define FONTEXT_MAGIC     0x31464754u  /* "TGF1", little-endian */
#define FONTEXT_WIDTH     5
#define FONTEXT_ROWS      10
#define FONTEXT_ASCENT    2
#define FONTEXT_ADVANCE   (FONTEXT_WIDTH + 1)

/* The file holds this header, and then one entry for each character. The
 * entries go up by their code point, thus a search divides the range.
 *
 * Each entry is one code point of 2 bytes, and then one column of 2 bytes for
 * each column of the cell. Bit 0 of a column is the row at the top.
 */

#define FONTEXT_HEADER_LEN  12
#define FONTEXT_ENTRY_LEN   (2 + (FONTEXT_WIDTH * 2))

/* The largest font that the board reads into memory. */

#define FONTEXT_MAX_GLYPHS  96

/****************************************************************************
 * Name: fontext_load
 *
 * Description:
 *   Read the extended font from a file into memory.
 *
 *   Note: a file that is absent, too large or not a font gives a negative
 *   value. The renderer then uses the font of the firmware alone.
 *
 ****************************************************************************/

int fontext_load(const char *path);

/****************************************************************************
 * Name: fontext_next
 *
 * Description:
 *   Take one character from a text in UTF-8, and give its columns.
 *   Give the count of the bytes that the character takes.
 *
 *   The function looks in the extended font first. A character that is absent
 *   there and inside the ASCII table comes from the font of the firmware. Any
 *   other character gives the columns of the space.
 *
 *   Note: the columns are those of the cell of FONTEXT_ROWS rows. The caller
 *   draws the row 0 at FONTEXT_ASCENT rows above the top of the letter.
 *
 ****************************************************************************/

size_t fontext_next(const char *s, size_t len, const uint16_t **cols);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H */
