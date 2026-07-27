/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_TM1629A_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_TM1629A_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* Twelve logical digit positions, 0 leftmost. */

#define TM1629A_NDIGITS 12

/****************************************************************************
 * Name: tm1629a_init
 *
 * Description:
 *   Drive the pins to their idle levels, clear the display and apply
 *   brightness (0-7).
 *
 ****************************************************************************/

void tm1629a_init(uint8_t brightness);

/* Brightness 0-7; `on` gates the display without disturbing the framebuffer. */

void tm1629a_setbrightness(uint8_t level, bool on);

/* Stage a raw segment mask (bit 0 = A .. bit 6 = G) or a character. Neither
 * touches the hardware until tm1629a_flush().
 */

void tm1629a_setraw(uint8_t digit, uint8_t segments);
void tm1629a_setchar(uint8_t digit, char c);

void tm1629a_clear(void);
void tm1629a_flush(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_TM1629A_H */
