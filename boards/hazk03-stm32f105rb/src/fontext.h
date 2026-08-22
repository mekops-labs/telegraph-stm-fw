/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The extended font, read from the flash, holding what ASCII does not. The
 * 5x7 font of the firmware is the fallback. Its cell is 10 rows for a letter
 * of 7: two above for an acute, one below for an ogonek.
 */

#define FONTEXT_MAGIC 0x31464754u /* "TGF1", little-endian */

/* The largest cell that a font in the flash may use. The file gives the cell
 * that it actually uses, thus one build carries a font of 5x7 for one line and
 * a smaller font for two lines.
 */

#define FONTEXT_WIDTH 5
#define FONTEXT_ROWS 10
#define FONTEXT_ASCENT 2
#define FONTEXT_ADVANCE (FONTEXT_WIDTH + 1)

/* This header, then an entry per character ordered by code point so a search
 * divides the range. An entry is [code point u16] then a u16 per column, bit
 * 0 the top row.
 */

#define FONTEXT_HEADER_LEN 12
#define FONTEXT_ENTRY_LEN (2 + (FONTEXT_WIDTH * 2))

/* The largest font that the board reads into memory. */

#define FONTEXT_MAX_GLYPHS 128

/* Read the extended font from a file. Negative when it is absent, too large
 * or not a font, and the renderer then uses the firmware's own.
 */

int fontext_load(const char *path);

/* Take one UTF-8 character and give its columns and its length in bytes. The
 * extended font first, then the firmware's for ASCII, then the space. Row 0
 * of the cell draws FONTEXT_ASCENT rows above the top of the letter.
 */

size_t fontext_next(const char *s, size_t len, const uint16_t **cols);

/* The cell of the font in use, or the firmware's without one from the flash.
 * A font carries its own cell, thus a compact one gives two lines on 14 rows.
 */

int fontext_width(void);
int fontext_rows(void);
int fontext_ascent(void);
int fontext_advance(void);

/****************************************************************************
 * Name: fontext_lineheight
 *
 * Description:
 *   Give the rows that one line of text needs.
 *
 ****************************************************************************/

int fontext_lineheight(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H */
