/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "stm32_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Display hardware pin map.
 *
 * TM1629A drives the 7-segment clock digits over a 3-wire serial bus.
 */

#define GPIO_TM1629A_STB  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN5)
#define GPIO_TM1629A_CLK  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN3)
#define GPIO_TM1629A_DIO  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN4)

/* SM1626D dot matrix. Both screens share the clock, output-enable and strobe
 * lines and differ only in their serial data input.
 */

#define GPIO_SM1626D_CLK  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTB | GPIO_PIN12)
#define GPIO_SM1626D_OE   (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN13)
#define GPIO_SM1626D_STB  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTB | GPIO_PIN14)
#define GPIO_SM1626D_DIN_MAIN \
                          (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTB | GPIO_PIN15)

/* Sub-screen data input. PA13 is SWDIO out of reset; see hazk03_jtag_reclaim
 * in stm32_bringup.c.
 */

#define GPIO_SM1626D_DIN_SUB \
                          (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTA | GPIO_PIN13)

/* DS3231 RTC. PC6/PC7 do not map to an I2C peripheral on this part, so the
 * bus is bit-banged; both lines are open-drain with board pull-ups.
 */

#define GPIO_DS3231_SCL   (GPIO_OUTPUT | GPIO_CNF_OUTOD | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTC | GPIO_PIN6)
#define GPIO_DS3231_SDA   (GPIO_OUTPUT | GPIO_CNF_OUTOD | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTC | GPIO_PIN7)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_bringup
 *
 * Description:
 *   Perform architecture-specific initialisation: reclaim the debug pins and
 *   place every display line in a defined state.
 *
 ****************************************************************************/

int stm32_bringup(void);

/****************************************************************************
 * Name: hazk03_display_init
 *
 * Description:
 *   Initialise the panels and the digits, and start the scan loop that keeps
 *   them lit.
 *
 ****************************************************************************/

int hazk03_display_init(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H */
