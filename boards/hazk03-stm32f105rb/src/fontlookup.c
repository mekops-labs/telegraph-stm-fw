/* SPDX-License-Identifier: Apache-2.0 */

/* The lookup of a character in a font, and the decoder of UTF-8. */

#include <nuttx/config.h>

#include <string.h>

#include "font5x7.h"
#include "fontext.h"

#define UTF8_MORE_MASK 0xc0
#define UTF8_MORE 0x80

/* Take one code point from a text in UTF-8. The function takes the sequences
 * of one, two and three bytes. A byte that starts no valid sequence gives the
 * code point of the space.
 */

static size_t fontext_utf8(const char *s, size_t len, uint32_t *cp) {
    const uint8_t *p = (const uint8_t *)s;

    if ((p[0] & 0x80) == 0) {
        *cp = p[0];
        return 1;
    }

    if ((p[0] & 0xe0) == 0xc0 && len >= 2 &&
        (p[1] & UTF8_MORE_MASK) == UTF8_MORE) {
        *cp = (uint32_t)((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        return 2;
    }

    if ((p[0] & 0xf0) == 0xe0 && len >= 3 &&
        (p[1] & UTF8_MORE_MASK) == UTF8_MORE &&
        (p[2] & UTF8_MORE_MASK) == UTF8_MORE) {
        *cp = (uint32_t)((p[0] & 0x0f) << 12) | (uint32_t)((p[1] & 0x3f) << 6) |
              (p[2] & 0x3f);
        return 3;
    }

    *cp = ' ';
    return 1;
}

/* The index of a code point in the font, or -1. The entries go up, thus the
 * search divides the range at each step.
 */

static int fontext_find(const struct fontext_s *font, uint32_t cp) {
    int lo = 0;
    int hi = (int)font->count - 1;

    if (cp > 0xffff) {
        return -1;
    }

    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        if (font->cp[mid] == (uint16_t)cp) {
            return mid;
        }

        if (font->cp[mid] < (uint16_t)cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return -1;
}

size_t fontext_next(const struct fontext_s *font, const char *s, size_t len,
                    uint16_t *cols) {
    uint32_t cp;
    size_t used = fontext_utf8(s, len, &cp);
    int idx = fontext_find(font, cp);
    int col;

    memset(cols, 0, FONTEXT_WIDTH * sizeof(cols[0]));

    if (idx >= 0) {
        const uint16_t *src = &font->cols[idx * font->stride];

        for (col = 0; col < font->width && col < FONTEXT_WIDTH; col++) {
            cols[col] = src[col];
        }

        return used;
    }

    if (font->ascii_fallback && cp >= FONT5X7_FIRST && cp <= FONT5X7_LAST) {
        const uint8_t *glyph = font5x7_glyph((char)cp);

        for (col = 0; col < FONT5X7_WIDTH; col++) {
            cols[col] = (uint16_t)(glyph[col] << font->ascent);
        }
    }

    return used;
}

int fontext_width(const struct fontext_s *font) { return font->width; }

int fontext_rows(const struct fontext_s *font) { return font->rows; }

int fontext_ascent(const struct fontext_s *font) { return font->ascent; }

int fontext_advance(const struct fontext_s *font) { return font->width + 1; }

int fontext_lineheight(const struct fontext_s *font) { return font->rows; }
