/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking *****************************************************************/

/* A crystal of 25 MHz, and CONFIG_HAZK03_CLOCK_HSE selects between two clock
 * trees. The connectivity-line code waits for a PLL without a limit, thus
 * this board supplies its own stm32_board_clockconfig() that gives up.
 */

#define STM32_BOARD_XTAL 25000000ul

#define STM32_HSI_FREQUENCY 8000000ul
#define STM32_LSI_FREQUENCY 40000
#define STM32_HSE_FREQUENCY STM32_BOARD_XTAL
#define STM32_LSE_FREQUENCY 32768

#ifdef CONFIG_HAZK03_CLOCK_HSE

/* The crystal path: 25 MHz / 5 -> PLL2 x8 -> / 5 -> PLL x9, thus 72 MHz. The
 * USB host needs exactly 48 MHz and its prescaler divides by 3, thus 72 MHz
 * is the only SYSCLK serving both the core and USB.
 */

#define STM32_PLL_PREDIV2 RCC_CFGR2_PREDIV2d5
#define STM32_PLL_PLL2MUL RCC_CFGR2_PLL2MULx8
#define STM32_PLL_PREDIV1 RCC_CFGR2_PREDIV1d5
#define STM32_PLL_PLLMUL RCC_CFGR_PLLMUL_CLKx9
#define STM32_PLL_FREQUENCY (72000000)

#define STM32_CFGR_OTGFSPRE RCC_CFGR_OTGFSPREd3

/* Two flash wait states. This value applies above 48 MHz. */

#define STM32_BOARD_FLASH_ACR_LATENCY FLASH_ACR_LATENCY_2

/* The APB2 clock is equal to HCLK, thus 72 MHz. That value is the maximum. */

#define STM32_RCC_CFGR_PPRE2 RCC_CFGR_PPRE2_HCLK

#else

/* The internal oscillator: 8 MHz / 2 -> PLL x9, thus 36 MHz and no USB clock.
 * That is its maximum, since the divider is fixed and x9 the largest
 * multiplier. PLL2 and PLL3 stay off, as they condition the crystal alone.
 */

#define STM32_PLL_PLLMUL RCC_CFGR_PLLMUL_CLKx9
#define STM32_PLL_FREQUENCY (36000000)

/* One flash wait state. This value applies from 24 MHz to 48 MHz. */

#define STM32_BOARD_FLASH_ACR_LATENCY FLASH_ACR_LATENCY_1

/* The APB2 clock is equal to HCLK. The value of 36 MHz is less than the
 * maximum of 72 MHz.
 */

#define STM32_RCC_CFGR_PPRE2 RCC_CFGR_PPRE2_HCLK

#endif

/* SYSCLK and HCLK are the PLL frequency */

#define STM32_SYSCLK_FREQUENCY STM32_PLL_FREQUENCY
#define STM32_HCLK_FREQUENCY STM32_PLL_FREQUENCY

#define STM32_PCLK2_FREQUENCY STM32_HCLK_FREQUENCY
#define STM32_APB2_CLKIN (STM32_PCLK2_FREQUENCY)

#define STM32_APB2_TIM1_CLKIN (STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM8_CLKIN (STM32_PCLK2_FREQUENCY)

/* The APB1 clock is HCLK/2. The maximum APB1 value is 36 MHz, thus this
 * divider serves both clock trees.
 */

#define STM32_RCC_CFGR_PPRE1 RCC_CFGR_PPRE1_HCLKd2
#define STM32_PCLK1_FREQUENCY (STM32_HCLK_FREQUENCY / 2)

/* If the APB1 prescaler is not 1, the APB1 timers run at two times PCLK1. */

#define STM32_APB1_TIM2_CLKIN (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM3_CLKIN (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM4_CLKIN (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM5_CLKIN (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM6_CLKIN (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM7_CLKIN (2 * STM32_PCLK1_FREQUENCY)

/* Timer frequencies for the matrix scan timer */

#define BOARD_TIM2_FREQUENCY STM32_APB1_TIM2_CLKIN
#define BOARD_TIM3_FREQUENCY STM32_APB1_TIM3_CLKIN
#define BOARD_TIM4_FREQUENCY STM32_APB1_TIM4_CLKIN

/* Alternate function pin selections ****************************************/

/* USART1 uses PA9 for TX and PA10 for RX. This is the link to the edge MCU.
 *
 * Note: the system bootloader uses the same two pins for the AN3155 protocol.
 */

#define GPIO_USART1_TX GPIO_USART1_TX_0
#define GPIO_USART1_RX GPIO_USART1_RX_0

/* SPI1 carries the W25Q32: PA5 SCK, PA6 MISO, PA7 MOSI, and PA4 as a GPIO
 * chip select, thus NSS stays unused. SPI2 is unavailable here, its PB13 and
 * PB15 carrying the output-enable and the data of the main panel.
 */

#define GPIO_SPI1_SCK GPIO_SPI1_SCK_0
#define GPIO_SPI1_MISO GPIO_SPI1_MISO_0
#define GPIO_SPI1_MOSI GPIO_SPI1_MOSI_0

/* OTG FS uses PA11 for D- and PA12 for D+. Its VBUS and identifier pins carry
 * the UART of the edge MCU instead: a host takes the identifier from its role
 * and the core takes an internal VBUS state.
 */

#define GPIO_OTGFS_DM GPIO_OTGFS_DM_0
#define GPIO_OTGFS_DP GPIO_OTGFS_DP_0

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H */
