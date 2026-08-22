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

/* Start a change of the image. The first change after a swap copies what the
 * scan reads, thus changing one part keeps the rest.
 */

void sm1626d_begin(struct sm1626d_dev_s *dev);

/* Give the changed image to the scan, which takes it at the start of the next
 * image, thus no image mixes the two.
 */

void sm1626d_commit(struct sm1626d_dev_s *dev);

/* Take the changed image if a writer gave one. The scan calls this at the
 * start of an image, and nothing else a writer also calls.
 */

void sm1626d_swapnow(struct sm1626d_dev_s *dev);

/* Put a rectangle of pixels into the image, row by row, each row starting at
 * a byte with bit 7 the pixel at the left.
 */

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

/* Scan the panel one time. The panel keeps an image only during a scan, thus
 * a caller calls this again and again.
 */

void sm1626d_refresh(struct sm1626d_dev_s *dev);

/* Send part of the bits of one row: the columns of the panel, then the
 * selection of that row. The transfer changes no light.
 */

void sm1626d_shiftbits(struct sm1626d_dev_s *dev, int row, int from, int count);

/* The count of the bits one row takes: the columns, then its selection. */

int sm1626d_rowbits(const struct sm1626d_dev_s *dev);

/* Send one row to both panels in one pass. They share the clock, thus a pass
 * for one alone leaves the other dark for that time.
 */

void sm1626d_shiftcombined(struct sm1626d_dev_s *main,
                           struct sm1626d_dev_s *sub, int row);

/* Move the bits of the shift register to the output. The two are separate,
 * thus a transfer changes no light.
 */

void sm1626d_latch(void);

/* Give light to the panels or take it away. Both share this line. */

void sm1626d_output(bool enable);

/* The time with light for one row in microseconds, at the brightness of this
 * panel. A panel that is off gives zero.
 */

int sm1626d_ontime(const struct sm1626d_dev_s *dev, int rowtime_us);

/* Draw a text with the 5x7 font, x,y the top left of the first character. It
 * stops at the right edge of the panel.
 */

void sm1626d_drawtext(struct sm1626d_dev_s *dev, int x, int y, const char *s,
                      size_t len);

/* The width of a text in pixels. The text is UTF-8, thus its characters are
 * not its bytes.
 */

int sm1626d_textwidth(const char *s, size_t len);

/* Draw a text into a bitmap of the caller: rows in order, each starting at a
 * byte, bit 7 the pixel at the left.
 */

void sm1626d_rendertext(uint8_t *bits, int w, int h, const char *s, size_t len);

/* Draw n texts into one bitmap of sm1626d_rendertext, one below the other,
 * each taking a line of the font's height.
 */

void sm1626d_rendertextlines(uint8_t *bits, int w, int totalh,
                             const size_t *starts, const size_t *lens,
                             const int *xoffs, int n, const char *s);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H */
