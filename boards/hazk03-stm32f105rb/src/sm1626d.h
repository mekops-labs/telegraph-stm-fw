/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* 16 scan rows; 10 bytes covers the 80 column bits the main screen shifts. */

#define SM1626D_ROWS      16
#define SM1626D_ROW_BYTES 10

/* Both screens share CLK, OE and STB and differ only in their data input, so
 * an instance is really just a framebuffer plus a DIN pin.
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
 *   Scan the panel once. The display holds an image only while it is being
 *   scanned, so this must be called continuously.
 *
 ****************************************************************************/

void sm1626d_refresh(struct sm1626d_dev_s *dev);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_SM1626D_H */
