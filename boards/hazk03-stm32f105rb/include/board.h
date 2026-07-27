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

/* This board runs entirely from the internal RC oscillator. Enabling HSE
 * hangs this part, so the external crystal is never started and the PLL is
 * fed from HSI/2:
 *
 *   HSI 8 MHz / 2 = 4 MHz -> PLL x9 -> SYSCLK 36 MHz
 *
 * The connectivity-line clock setup in arch/arm/src/stm32 always drives the
 * PLL from PREDIV1 (the HSE path) and starts PLL2/PLL3, so this board
 * supplies its own stm32_board_clockconfig() and selects
 * CONFIG_ARCH_BOARD_STM32_CUSTOM_CLOCKCONFIG. PLL2 and PLL3 stay off; they
 * exist only to condition the HSE input this board does not use.
 *
 * 36 MHz is the ceiling for a HSI-sourced PLL here: the multiplier acts on
 * HSI/2, and x9 is the largest value that keeps SYSCLK within spec.
 */

#define STM32_BOARD_XTAL        8000000ul            /* Nominal, unused - HSE stays off */

#define STM32_HSI_FREQUENCY     8000000ul
#define STM32_LSI_FREQUENCY     40000
#define STM32_HSE_FREQUENCY     STM32_BOARD_XTAL
#define STM32_LSE_FREQUENCY     32768

/* PLL fed from HSI/2, multiplied to 36 MHz */

#define STM32_PLL_PLLMUL        RCC_CFGR_PLLMUL_CLKx9
#define STM32_PLL_FREQUENCY     (36000000)

/* SYSCLK and HCLK are the PLL frequency */

#define STM32_SYSCLK_FREQUENCY  STM32_PLL_FREQUENCY
#define STM32_HCLK_FREQUENCY    STM32_PLL_FREQUENCY

/* APB2 (PCLK2) is HCLK - 36 MHz, within the 72 MHz APB2 ceiling */

#define STM32_RCC_CFGR_PPRE2    RCC_CFGR_PPRE2_HCLK
#define STM32_PCLK2_FREQUENCY   STM32_HCLK_FREQUENCY
#define STM32_APB2_CLKIN        (STM32_PCLK2_FREQUENCY)

#define STM32_APB2_TIM1_CLKIN   (STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM8_CLKIN   (STM32_PCLK2_FREQUENCY)

/* APB1 (PCLK1) is HCLK/2 - 18 MHz. APB1 is capped at 36 MHz on this part;
 * the divide keeps margin and matches the reverse-engineered configuration.
 */

#define STM32_RCC_CFGR_PPRE1    RCC_CFGR_PPRE1_HCLKd2
#define STM32_PCLK1_FREQUENCY   (STM32_HCLK_FREQUENCY / 2)

/* APB1 timers run at twice PCLK1 when the APB1 prescaler is not 1 */

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

/* USART1: PA9 (TX) / PA10 (RX) - the link to the edge MCU, and the same
 * pins the STM32 system bootloader uses for AN3155 flashing.
 */

#define GPIO_USART1_TX          GPIO_USART1_TX_0
#define GPIO_USART1_RX          GPIO_USART1_RX_0

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H */
