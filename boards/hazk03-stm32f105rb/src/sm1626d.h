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

#define SM1626D_ROWS      16

/* The brightness level. The value 0 is the lowest, and it stays visible. */

#define SM1626D_BRIGHT_MAX 7
#define SM1626D_ROW_BYTES 10

/* Both screens share the CLK, OE and STB signals. Only the data input is
 * different. Thus one instance holds a framebuffer and a data pin.
 */

struct sm1626d_dev_s
{
  uint32_t din;                                       /* GPIO pin config */
  uint8_t width;
  uint8_t height;
  uint8_t bright;
  uint8_t fb[SM1626D_ROWS][SM1626D_ROW_BYTES];
};

void sm1626d_init(struct sm1626d_dev_s *dev, uint32_t din,
                  uint8_t width, uint8_t height);

/* Set the brightness. The driver makes the on-time of each row shorter. */

void sm1626d_setbrightness(struct sm1626d_dev_s *dev, uint8_t level);

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

void sm1626d_drawtext(struct sm1626d_dev_s *dev, int x, int y,
                      const char *s, size_t len);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H */
