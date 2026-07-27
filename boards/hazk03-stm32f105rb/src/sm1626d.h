/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* There are 16 scan rows. The main screen sends 80 column bits, thus 10 bytes
 * are sufficient for one row.
 */

#define SM1626D_ROWS      16
#define SM1626D_ROW_BYTES 10

/* Both screens share the CLK, OE and STB signals. Only the data input is
 * different. Thus one instance holds a framebuffer and a data pin.
 */

struct sm1626d_dev_s
{
  uint32_t din;                                       /* GPIO pin config */
  uint8_t width;
  uint8_t height;
  uint8_t fb[SM1626D_ROWS][SM1626D_ROW_BYTES];
};

void sm1626d_init(struct sm1626d_dev_s *dev, uint32_t din,
                  uint8_t width, uint8_t height);

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

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H */
