/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_TM1629A_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_TM1629A_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* There are twelve logical digit positions. Position 0 is at the left. */

#define TM1629A_NDIGITS 12

/****************************************************************************
 * Name: tm1629a_init
 *
 * Description:
 *   Set the pins to their idle levels.
 *   Clear the display.
 *   Set the brightness. Permitted values are 0 to 7.
 *
 ****************************************************************************/

void tm1629a_init(uint8_t brightness);

/* Set the brightness. Permitted values are 0 to 7.
 *
 * Note: the `on` parameter turns the display on or off. It does not change
 * the framebuffer.
 */

void tm1629a_setbrightness(uint8_t level, bool on);

/* Put a segment mask or a character into the framebuffer. In the mask, bit 0
 * is segment A and bit 6 is segment G.
 *
 * Note: these two functions do not write to the hardware. The function
 * tm1629a_flush() writes the framebuffer to the part.
 */

void tm1629a_setraw(uint8_t digit, uint8_t segments);
void tm1629a_setchar(uint8_t digit, char c);

void tm1629a_clear(void);
void tm1629a_flush(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_TM1629A_H */
