/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* There are 16 scan rows. The main screen sends 80 column bits, thus 10 bytes
 * are sufficient for one row.
 */

#define SM1626D_ROWS 16

/* The brightness level. The value 0 is the lowest, and it stays visible. */

#define SM1626D_BRIGHT_MAX 7
#define SM1626D_ROW_BYTES 10

/* Both screens share the CLK, OE and STB signals. Only the data input is
 * different. Thus one instance holds a framebuffer and a data pin.
 */

struct sm1626d_dev_s {
    uint32_t din;      /* GPIO pin config */
    uint32_t din_bsrr; /* set-reset register of the pin */
    uint32_t din_set;
    uint32_t din_clr;
    uint8_t width;
    uint8_t height;
    uint8_t bright;
    bool on;

    /* The panel keeps two images. The scan reads one of them and a writer
     * changes the other, thus no scan ever shows a partial image.
     */

    uint8_t front;      /* the image that the scan reads                  */
    bool dirty;         /* a writer has changed the other image           */
    volatile bool swap; /* the scan takes the other image at the next one */

    uint8_t fb[2][SM1626D_ROWS][SM1626D_ROW_BYTES];
};

/****************************************************************************
 * Name: sm1626d_begin
 *
 * Description:
 *   Start a change of the image. The first change after a swap copies the
 *   image that the scan reads, thus a change of one part keeps the rest.
 *
 ****************************************************************************/

void sm1626d_begin(struct sm1626d_dev_s *dev);

/****************************************************************************
 * Name: sm1626d_commit
 *
 * Description:
 *   Give the changed image to the scan. The scan takes it at the start of the
 *   next image, thus no image ever mixes the two.
 *
 ****************************************************************************/

void sm1626d_commit(struct sm1626d_dev_s *dev);

/****************************************************************************
 * Name: sm1626d_swapnow
 *
 * Description:
 *   Take the changed image, if a writer gave one. The scan calls this at the
 *   start of an image, and it calls nothing else that a writer also calls.
 *
 ****************************************************************************/

void sm1626d_swapnow(struct sm1626d_dev_s *dev);

/****************************************************************************
 * Name: sm1626d_drawbitmap
 *
 * Description:
 *   Put a rectangle of pixels into the image. The bits go row by row, and
 *   each row starts at a byte. Bit 7 of a byte is the pixel at the left.
 *
 ****************************************************************************/

void sm1626d_drawbitmap(struct sm1626d_dev_s *dev, int x, int y, int w, int h,
                        const uint8_t *bits);

void sm1626d_init(struct sm1626d_dev_s *dev, uint32_t din, uint8_t width,
                  uint8_t height);

/* Set the brightness. The driver makes the on-time of each row shorter.
 *
 * Note: the `on` parameter turns the panel on or off. It does not change the
 * framebuffer.
 */

void sm1626d_setbrightness(struct sm1626d_dev_s *dev, uint8_t level, bool on);

void sm1626d_clear(struct sm1626d_dev_s *dev);
void sm1626d_drawpixel(struct sm1626d_dev_s *dev, int x, int y, bool on);

/****************************************************************************
 * Name: sm1626d_refresh
 *
 * Description:
 *   Scan the panel one time.
 *
 *   Note: the panel keeps an image only during a scan. Thus the caller calls
 *   this function again and again.
 *
 ****************************************************************************/

void sm1626d_refresh(struct sm1626d_dev_s *dev);

/****************************************************************************
 * Name: sm1626d_shiftrow
 *
 * Description:
 *   Send part of the bits of one row. The bits of a row are the columns of
 *   the panel and then the selection of that row.
 *
 *   Note: the transfer changes no light. Thus the caller sends the next row
 *   in parts while the panel holds the row of the last latch, and no part
 *   holds the CPU for long.
 *
 ****************************************************************************/

void sm1626d_shiftbits(struct sm1626d_dev_s *dev, int row, int from, int count);

/****************************************************************************
 * Name: sm1626d_rowbits
 *
 * Description:
 *   Give the count of the bits that one row takes: the columns of the panel
 *   and then the selection of the row.
 *
 ****************************************************************************/

int sm1626d_rowbits(const struct sm1626d_dev_s *dev);

/****************************************************************************
 * Name: sm1626d_shiftcombined
 *
 * Description:
 *   Send one row to both panels in one pass. The panels share the clock, thus
 *   a pass for one panel alone leaves the other panel dark for that time.
 *
 *   Note: one image then takes 16 rows and not 32. Thus the rate of the image
 *   doubles, and each panel keeps its light through the whole row.
 *
 ****************************************************************************/

void sm1626d_shiftcombined(struct sm1626d_dev_s *main,
                           struct sm1626d_dev_s *sub, int row);

/****************************************************************************
 * Name: sm1626d_latch
 *
 * Description:
 *   Move the bits of the shift register to the output of the panel.
 *
 *   Note: the shift register and that output are separate. Thus a transfer
 *   changes no light, and the panel keeps the row of the last latch.
 *
 ****************************************************************************/

void sm1626d_latch(void);

/****************************************************************************
 * Name: sm1626d_output
 *
 * Description:
 *   Give light to the panels, or take it away. Both panels share this line.
 *
 ****************************************************************************/

void sm1626d_output(bool enable);

/****************************************************************************
 * Name: sm1626d_ontime
 *
 * Description:
 *   Give the time with light for one row, in microseconds, at the brightness
 *   of this panel. A panel that is off gives zero.
 *
 ****************************************************************************/

int sm1626d_ontime(const struct sm1626d_dev_s *dev, int rowtime_us);

/****************************************************************************
 * Name: sm1626d_drawtext
 *
 * Description:
 *   Draw a text with the 5x7 font. The position x,y is the top left corner of
 *   the first character.
 *
 *   Note: the function stops at the right edge of the panel. It draws no
 *   partial character.
 *
 ****************************************************************************/

void sm1626d_drawtext(struct sm1626d_dev_s *dev, int x, int y, const char *s,
                      size_t len);

/****************************************************************************
 * Name: sm1626d_textwidth
 *
 * Description:
 *   Give the width of a text in pixels.
 *
 *   Note: the text is in UTF-8, thus the count of the characters is not the
 *   count of the bytes.
 *
 ****************************************************************************/

int sm1626d_textwidth(const char *s, size_t len);

/****************************************************************************
 * Name: sm1626d_rendertext
 *
 * Description:
 *   Draw a text into a bitmap that belongs to the caller. The rows follow
 *   each other, each row starts at a byte, and bit 7 of a byte is the pixel
 *   at the left.
 *
 *   Note: the animation uses this to hold a message wider than the panel.
 *   Thus a scrolling message costs one frame and not one for each step.
 *
 ****************************************************************************/

void sm1626d_rendertext(uint8_t *bits, int w, int h, const char *s, size_t len);

/****************************************************************************
 * Name: sm1626d_rendertextlines
 *
 * Description:
 *   Draw n texts into the same bitmap of sm1626d_rendertext, one below the
 *   other. Each text s[starts[k]..starts[k]+lens[k]) takes one line, in the
 *   font's own line height, starting at column xoffs[k]. The caller wraps
 *   the text into lines and places each one horizontally; this function
 *   only draws already-placed lines.
 *
 ****************************************************************************/

void sm1626d_rendertextlines(uint8_t *bits, int w, int totalh,
                             const size_t *starts, const size_t *lens,
                             const int *xoffs, int n, const char *s);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H */
