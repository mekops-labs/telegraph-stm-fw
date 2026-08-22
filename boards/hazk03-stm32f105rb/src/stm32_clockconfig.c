/* SPDX-License-Identifier: Apache-2.0 */

/* Clock setup. Every wait here ends, where the connectivity-line code waits
 * for a PLL forever and stops the boot with no sign of the cause; the failure
 * keeps the internal oscillator. CONFIG_HAZK03_CLOCK_HSE selects the source.
 */

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_flash.h"
#include "stm32_rcc.h"

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* These limits stop the wait loops. If a clock does not become ready, the
 * function returns instead of a wait without an end.
 */

#define HSIRDY_TIMEOUT (1000 * 1000)
#define HSERDY_TIMEOUT (1000 * 1000)
#define PLLRDY_TIMEOUT (1000 * 1000)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Wait for a ready bit in the clock control register. Give true when the bit
 * arrives before the end of the wait.
 */

static bool clock_ready(uint32_t bit) {
    int32_t timeout;

    for (timeout = PLLRDY_TIMEOUT; timeout > 0; timeout--) {
        if ((getreg32(STM32_RCC_CR) & bit) != 0) {
            return true;
        }
    }

    return false;
}

#ifdef CONFIG_HAZK03_CLOCK_HSE

/* Start the crystal, then condition it with PLL2 into the 8 MHz input that
 * the main PLL needs. Give false when the crystal or PLL2 does not come up.
 */

static bool clock_hse_input(void) {
    uint32_t regval;
    int32_t timeout;

    regval = getreg32(STM32_RCC_CR);
    regval &= ~RCC_CR_HSEBYP;
    regval |= RCC_CR_HSEON;
    putreg32(regval, STM32_RCC_CR);

    for (timeout = HSERDY_TIMEOUT; timeout > 0; timeout--) {
        if ((getreg32(STM32_RCC_CR) & RCC_CR_HSERDY) != 0) {
            break;
        }
    }

    if (timeout <= 0) {
        return false;
    }

    /* The crystal goes to PREDIV2, then PLL2, then PREDIV1. The output of
     * PREDIV1 is the input of the main PLL.
     */

    regval = getreg32(STM32_RCC_CFGR2);
    regval &= ~(RCC_CFGR2_PREDIV2_MASK | RCC_CFGR2_PLL2MUL_MASK |
                RCC_CFGR2_PREDIV1SRC_MASK | RCC_CFGR2_PREDIV1_MASK);
    regval |= (STM32_PLL_PREDIV2 | STM32_PLL_PLL2MUL |
               RCC_CFGR2_PREDIV1SRC_PLL2 | STM32_PLL_PREDIV1);
    putreg32(regval, STM32_RCC_CFGR2);

    regval = getreg32(STM32_RCC_CR);
    regval |= RCC_CR_PLL2ON;
    putreg32(regval, STM32_RCC_CR);

    return clock_ready(RCC_CR_PLL2RDY);
}

#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Build the clock tree from the source the configuration selects. This runs
 * early, with the chip on the internal oscillator and no PLL.
 */

void stm32_board_clockconfig(void) {
    uint32_t regval;
    int32_t timeout;

    /* Enable the HSI oscillator.
     *
     * Note: the part boots on the HSI. This step is explicit because the PLL
     * accepts a new configuration only when it is not the system clock.
     */

    regval = getreg32(STM32_RCC_CR);
    regval |= RCC_CR_HSION;
    putreg32(regval, STM32_RCC_CR);

    for (timeout = HSIRDY_TIMEOUT; timeout > 0; timeout--) {
        if ((getreg32(STM32_RCC_CR) & RCC_CR_HSIRDY) != 0) {
            break;
        }
    }

    if (timeout <= 0) {
        return;
    }

    /* Select the HSI as the system clock. Then stop the PLL. */

    regval = getreg32(STM32_RCC_CFGR);
    regval &= ~RCC_CFGR_SW_MASK;
    regval |= RCC_CFGR_SW_HSI;
    putreg32(regval, STM32_RCC_CFGR);

    while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_HSI)
        ;

    regval = getreg32(STM32_RCC_CR);
    regval &= ~RCC_CR_PLLON;
    putreg32(regval, STM32_RCC_CR);

    /* The bus prescalers: AHB takes SYSCLK, APB2 HCLK, APB1 HCLK/2. Set
     * before SYSCLK rises, thus no bus runs past its maximum.
     */

    regval = getreg32(STM32_RCC_CFGR);
    regval &= ~(RCC_CFGR_HPRE_MASK | RCC_CFGR_PPRE1_MASK | RCC_CFGR_PPRE2_MASK);
    regval |=
        (RCC_CFGR_HPRE_SYSCLK | STM32_RCC_CFGR_PPRE1 | STM32_RCC_CFGR_PPRE2);
    putreg32(regval, STM32_RCC_CFGR);

    /* Set the flash wait states for the frequency that follows. */

    regval = getreg32(STM32_FLASH_ACR);
    regval &= ~FLASH_ACR_LATENCY_MASK;
    regval |= (STM32_BOARD_FLASH_ACR_LATENCY | FLASH_ACR_PRTFBE);
    putreg32(regval, STM32_FLASH_ACR);

    /* Select the input of the main PLL. A clear PLLSRC bit selects HSI/2, and
     * a set bit selects the PREDIV1 path from the crystal.
     */

#ifdef CONFIG_HAZK03_CLOCK_HSE
    if (!clock_hse_input()) {
        return;
    }
#endif

    regval = getreg32(STM32_RCC_CFGR);
    regval &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMUL_MASK);
#ifdef CONFIG_HAZK03_CLOCK_HSE
    regval |= RCC_CFGR_PLLSRC;
#endif
    regval |= STM32_PLL_PLLMUL;
    putreg32(regval, STM32_RCC_CFGR);

    regval = getreg32(STM32_RCC_CR);
    regval |= RCC_CR_PLLON;
    putreg32(regval, STM32_RCC_CR);

    if (!clock_ready(RCC_CR_PLLRDY)) {
        /* If the PLL does not lock, keep the HSI at 8 MHz as the system
         * clock.
         */

        return;
    }

    regval = getreg32(STM32_RCC_CFGR);
    regval &= ~RCC_CFGR_SW_MASK;
    regval |= RCC_CFGR_SW_PLL;
    putreg32(regval, STM32_RCC_CFGR);

    while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL)
        ;
}
