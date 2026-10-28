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

/* One font: a sorted table of code points and the columns of each. The font in
 * the flash and the one built into the firmware share this type, and every
 * drawing call names the font it uses.
 */

struct fontext_s {
    const uint16_t *cp;   /* code points, ascending                    */
    const uint16_t *cols; /* `stride` columns per entry, bit 0 the top */
    uint16_t count;
    uint8_t width; /* columns in use for a character */
    uint8_t rows;
    uint8_t ascent; /* rows of the cell above the top of a letter */
    uint8_t stride; /* columns stored per entry                   */

    /* ASCII that the font lacks comes from the 5x7 font of the firmware; false
     * gives a space.
     */

    bool ascii_fallback;
};

/* The font read from the flash, drawn on both panels for a single line. It
 * starts with no entries, thus every ASCII character comes from the firmware's.
 */

const struct fontext_s *fontext_big(void);

/* The font built into the firmware for two lines on 14 rows. */

const struct fontext_s *fontext_compact(void);

/* Read the big font from a file into a second table, and publish that table
 * only when the whole file is good. Thus a bad file leaves the font as it was,
 * and a call that took fontext_big() before keeps a consistent table. Negative
 * when the file is absent, too large, unordered or not a font.
 */

int fontext_load(const char *path);

/* Take one UTF-8 character and copy its FONTEXT_WIDTH columns to `cols`; give
 * its length in bytes. Row 0 of the cell draws `ascent` rows above the top of
 * the letter.
 */

size_t fontext_next(const struct fontext_s *font, const char *s, size_t len,
                    uint16_t *cols);

/* The cell of a font. A font carries its own cell, thus a compact one gives
 * two lines on 14 rows.
 */

int fontext_width(const struct fontext_s *font);
int fontext_rows(const struct fontext_s *font);
int fontext_ascent(const struct fontext_s *font);
int fontext_advance(const struct fontext_s *font);

/* The rows that one line of text needs. */

int fontext_lineheight(const struct fontext_s *font);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONTEXT_H */
