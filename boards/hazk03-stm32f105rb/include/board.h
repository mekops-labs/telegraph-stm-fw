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

/* This board uses the internal RC oscillator only.
 *
 * Note: the HSE crystal makes this part stop. Thus the firmware does not
 * start the crystal, and the PLL gets its input from HSI/2.
 *
 * Note: the clock path is HSI 8 MHz, then a divide by 2, then the PLL
 * multiplier x9. The result is a SYSCLK of 36 MHz.
 *
 * Note: the connectivity-line clock code in arch/arm/src/stm32 always drives
 * the PLL from PREDIV1. That path uses the HSE input. Thus this board supplies
 * a custom stm32_board_clockconfig() function.
 *
 * Note: PLL2 and PLL3 stay off. These two PLLs only condition the HSE input.
 *
 * Note: 36 MHz is the maximum SYSCLK for an HSI source. The multiplier acts on
 * HSI/2, and x9 is the largest permitted value.
 */

#define STM32_BOARD_XTAL        8000000ul   /* Not used. The HSE stays off. */

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

/* The APB2 clock is equal to HCLK. The value of 36 MHz is less than the
 * maximum of 72 MHz.
 */

#define STM32_RCC_CFGR_PPRE2    RCC_CFGR_PPRE2_HCLK
#define STM32_PCLK2_FREQUENCY   STM32_HCLK_FREQUENCY
#define STM32_APB2_CLKIN        (STM32_PCLK2_FREQUENCY)

#define STM32_APB2_TIM1_CLKIN   (STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM8_CLKIN   (STM32_PCLK2_FREQUENCY)

/* The APB1 clock is HCLK/2, thus 18 MHz. The maximum APB1 value is 36 MHz.
 *
 * Note: this divider keeps a margin. It also agrees with the reverse
 * engineered firmware.
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

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_INCLUDE_BOARD_H */
