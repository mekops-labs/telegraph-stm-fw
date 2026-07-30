/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONT5X7_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONT5X7_H

#include <nuttx/config.h>

#include <stdint.h>

/* A 5x7 font for the printable ASCII characters.
 *
 * Note: the panels have 14 rows. Thus a font of 7 rows gives one line of text
 * with space above it and below it.
 */

#define FONT5X7_WIDTH 5
#define FONT5X7_HEIGHT 7
#define FONT5X7_ADVANCE (FONT5X7_WIDTH + 1)
#define FONT5X7_FIRST 0x20
#define FONT5X7_LAST 0x7e

/****************************************************************************
 * Name: font5x7_glyph
 *
 * Description:
 *   Give the 5 columns of one character. Bit 0 is the row at the top.
 *
 *   Note: a character outside the range gives the columns of the space.
 *
 ****************************************************************************/

const uint8_t *font5x7_glyph(char c);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_FONT5X7_H */
