/* SPDX-License-Identifier: Apache-2.0 */

/* The text renderer of the panels, separate from the driver so a build with
 * no text pays no flash for the font. The text is UTF-8, the extended font
 * giving what ASCII does not.
 */

#include <nuttx/config.h>

#include <string.h>

#include "font5x7.h"
#include "fontext.h"
#include "sm1626d.h"

int sm1626d_textwidth(const struct fontext_s *font, const char *s, size_t len) {
    uint16_t cols[FONTEXT_WIDTH];
    size_t i = 0;
    int count = 0;

    while (i < len) {
        i += fontext_next(font, &s[i], len - i, cols);
        count++;
    }

    if (count == 0) {
        return 0;
    }

    /* Each character takes its columns, and a gap follows each one except the
     * last.
     */

    return (count * fontext_advance(font)) - 1;
}

void sm1626d_drawtext(struct sm1626d_dev_s *dev, const struct fontext_s *font,
                      int x, int y, const char *s, size_t len) {
    size_t i = 0;

    /* The cell is taller than the letter. Thus the top of the cell goes above
     * the top of the letter, and a mark such as an acute has its own rows.
     */

    y -= fontext_ascent(font);

    while (i < len) {
        uint16_t cols[FONTEXT_WIDTH];
        int col;

        i += fontext_next(font, &s[i], len - i, cols);

        for (col = 0; col < fontext_width(font); col++) {
            int row;

            for (row = 0; row < fontext_rows(font); row++) {
                if (cols[col] & (1u << row)) {
                    sm1626d_drawpixel(dev, x + col, y + row, true);
                }
            }
        }

        x += fontext_advance(font);

        if (x >= dev->width) {
            break;
        }
    }
}

/* Draw one text into bits, at the cell whose top is ytop and whose height is
 * cellheight. Both sm1626d_rendertext() and sm1626d_rendertextlines() place
 * one text this way; the second calls this once per wrapped line.
 */

static void rendertext_at(const struct fontext_s *font, uint8_t *bits, int w,
                          int totalh, int stride, int xoff, int ytop,
                          int cellheight, const char *s, size_t len) {
    size_t i = 0;
    int x = xoff;

    while (i < len && x < w) {
        uint16_t cols[FONTEXT_WIDTH];
        int col;

        i += fontext_next(font, &s[i], len - i, cols);

        for (col = 0; col < fontext_width(font); col++) {
            int row;

            if (x + col >= w) {
                break;
            }

            for (row = 0; row < fontext_rows(font); row++) {
                /* The cell is taller than the letter. The row 0 of the cell
                 * belongs above the letter, thus it moves down here.
                 */

                int y = ytop + row - fontext_ascent(font) +
                        ((cellheight -
                          (fontext_rows(font) - fontext_ascent(font) - 1)) /
                         2);

                if (y < 0 || y >= totalh) {
                    continue;
                }

                if (cols[col] & (1u << row)) {
                    bits[(y * stride) + ((x + col) / 8)] |=
                        (0x80 >> ((x + col) % 8));
                }
            }
        }

        x += fontext_advance(font);
    }
}

void sm1626d_rendertext(const struct fontext_s *font, uint8_t *bits, int w,
                        int h, const char *s, size_t len) {
    int stride = (w + 7) / 8;

    memset(bits, 0, (size_t)(stride * h));
    rendertext_at(font, bits, w, h, stride, 0, 0, h, s, len);
}

void sm1626d_rendertextlines(const struct fontext_s *font, uint8_t *bits, int w,
                             int totalh, const size_t *starts,
                             const size_t *lens, const int *xoffs, int n,
                             const char *s) {
    int stride = (w + 7) / 8;
    int lineheight = fontext_lineheight(font);
    int k;

    memset(bits, 0, (size_t)(stride * totalh));

    for (k = 0; k < n; k++) {
        rendertext_at(font, bits, w, totalh, stride, xoffs[k], k * lineheight,
                      lineheight, &s[starts[k]], lens[k]);
    }
}
