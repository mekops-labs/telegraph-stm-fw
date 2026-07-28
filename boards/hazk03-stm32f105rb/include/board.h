/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking *****************************************************************/

/* The board carries a crystal of 25 MHz. Two clock trees are available, and
 * CONFIG_HAZK03_CLOCK_HSE selects between them.
 *
 * Note: the connectivity-line clock code in arch/arm/src/stm32 always drives
 * the PLL from PREDIV1, and it waits for each PLL without a limit. Thus this
 * board supplies a custom stm32_board_clockconfig() function, which gives up
 * and keeps the internal oscillator when a PLL does not lock.
 */

#define STM32_BOARD_XTAL        25000000ul

#define STM32_HSI_FREQUENCY     8000000ul
#define STM32_LSI_FREQUENCY     40000
#define STM32_HSE_FREQUENCY     STM32_BOARD_XTAL
#define STM32_LSE_FREQUENCY     32768

#ifdef CONFIG_HAZK03_CLOCK_HSE

/* The crystal path gives 72 MHz, and a clock of 48 MHz for the USB host.
 *
 * The crystal of 25 MHz goes to PREDIV2 with a divider of 5, thus 5 MHz.
 * PLL2 multiplies that by 8, thus 40 MHz. PREDIV1 takes PLL2 with a divider
 * of 5, thus 8 MHz. The main PLL multiplies that by 9, thus 72 MHz.
 *
 * Note: the USB host needs exactly 48 MHz, and the OTG prescaler takes the
 * PLL oscillator at three times SYSCLK and divides it by 3. Thus 72 MHz is
 * the only SYSCLK that serves both the core and USB.
 */

#define STM32_PLL_PREDIV2       RCC_CFGR2_PREDIV2d5
#define STM32_PLL_PLL2MUL       RCC_CFGR2_PLL2MULx8
#define STM32_PLL_PREDIV1       RCC_CFGR2_PREDIV1d5
#define STM32_PLL_PLLMUL        RCC_CFGR_PLLMUL_CLKx9
#define STM32_PLL_FREQUENCY     (72000000)

#define STM32_CFGR_OTGFSPRE     RCC_CFGR_OTGFSPREd3

/* Two flash wait states. This value applies above 48 MHz. */

#define STM32_BOARD_FLASH_ACR_LATENCY FLASH_ACR_LATENCY_2

/* The APB2 clock is equal to HCLK, thus 72 MHz. That value is the maximum. */

#define STM32_RCC_CFGR_PPRE2    RCC_CFGR_PPRE2_HCLK

#else

/* The internal oscillator gives 36 MHz, and no clock for the USB host.
 *
 * The HSI of 8 MHz goes through a fixed divider of 2, thus 4 MHz. The main
 * PLL multiplies that by 9, thus 36 MHz.
 *
 * Note: 36 MHz is the maximum SYSCLK from this source. The divider before the
 * PLL is fixed, and x9 is the largest multiplier on this part.
 *
 * Note: PLL2 and PLL3 stay off. These two PLLs only condition the crystal.
 */

#define STM32_PLL_PLLMUL        RCC_CFGR_PLLMUL_CLKx9
#define STM32_PLL_FREQUENCY     (36000000)

/* One flash wait state. This value applies from 24 MHz to 48 MHz. */

#define STM32_BOARD_FLASH_ACR_LATENCY FLASH_ACR_LATENCY_1

/* The APB2 clock is equal to HCLK. The value of 36 MHz is less than the
 * maximum of 72 MHz.
 */

#define STM32_RCC_CFGR_PPRE2    RCC_CFGR_PPRE2_HCLK

#endif

/* SYSCLK and HCLK are the PLL frequency */

#define STM32_SYSCLK_FREQUENCY  STM32_PLL_FREQUENCY
#define STM32_HCLK_FREQUENCY    STM32_PLL_FREQUENCY

#define STM32_PCLK2_FREQUENCY   STM32_HCLK_FREQUENCY
#define STM32_APB2_CLKIN        (STM32_PCLK2_FREQUENCY)

#define STM32_APB2_TIM1_CLKIN   (STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM8_CLKIN   (STM32_PCLK2_FREQUENCY)

/* The APB1 clock is HCLK/2. The maximum APB1 value is 36 MHz, thus this
 * divider serves both clock trees.
 */

#define STM32_RCC_CFGR_PPRE1    RCC_CFGR_PPRE1_HCLKd2
#define STM32_PCLK1_FREQUENCY   (STM32_HCLK_FREQUENCY / 2)

/* If the APB1 prescaler is not 1, the APB1 timers run at two times PCLK1. */

#define STM32_APB1_TIM2_CLKIN   (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM3_CLKIN   (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM4_CLKIN   (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM5_CLKIN   (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM6_CLKIN   (2 * STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM7_CLKIN   (2 * STM32_PCLK1_FREQUENCY)

/* Timer frequencies for the matrix scan timer */

#define BOARD_TIM2_FREQUENCY    STM32_APB1_TIM2_CLKIN
#define BOARD_TIM3_FREQUENCY    STM32_APB1_TIM3_CLKIN
#define BOARD_TIM4_FREQUENCY    STM32_APB1_TIM4_CLKIN

/* Alternate function pin selections ****************************************/

/* USART1 uses PA9 for TX and PA10 for RX. This is the link to the edge MCU.
 *
 * Note: the system bootloader uses the same two pins for the AN3155 protocol.
 */

#define GPIO_USART1_TX          GPIO_USART1_TX_0
#define GPIO_USART1_RX          GPIO_USART1_RX_0

/* SPI1 uses PA5 for SCK, PA6 for MISO and PA7 for MOSI. The bus carries the
 * W25Q32 flash.
 *
 * Note: the chip-select line is PA4, and the board drives it as a GPIO. Thus
 * the NSS signal of the peripheral stays unused.
 *
 * Note: SPI2 is not available for this function. Its pins PB13 and PB15 carry
 * the output-enable and the data of the main panel.
 */

#define GPIO_SPI1_SCK           GPIO_SPI1_SCK_0
#define GPIO_SPI1_MISO          GPIO_SPI1_MISO_0
#define GPIO_SPI1_MOSI          GPIO_SPI1_MOSI_0

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H */
