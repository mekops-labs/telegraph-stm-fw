/* SPDX-License-Identifier: Apache-2.0 */

/* A probe of the external crystal.
 *
 * Note: the USB host of this part needs a clock of 48 MHz, and a PLL that
 * takes the HSE oscillator is the only source of it. The HSI gives at most
 * 36 MHz, because the PLL takes HSI/2 of 4 MHz and its largest multiplier on
 * this part is 9. Thus the crystal decides whether this board reaches USB.
 *
 * Note: this probe leaves the system clock on the HSI. It starts the crystal
 * and it reads the frequency, and it changes no divider. A crystal that does
 * not start thus costs nothing.
 *
 * Note: the frequency comes from the hardware and not from a marking on the
 * part. The clock output on PA8 carries the crystal, and TIM1 counts the
 * edges of that same pin while TIM2 measures the time. The pin drives its own
 * input, thus this needs no wire.
 */

#include <nuttx/config.h>

#include <stdio.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_gpio.h"
#include "stm32_rcc.h"

#include "hardware/stm32_tim.h"

#include "hazk03.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The crystal gets this long to start. A crystal starts in a few
 * milliseconds, and a board without one never sets the ready bit.
 */

#define HSE_READY_SPINS 1000000

/* The gate of the count. TIM2 runs at 1 MHz, thus this value is microseconds.
 *
 * Note: TIM1 counts 16 bits. A crystal of 25 MHz gives 50000 edges in this
 * time, thus the counter stays below its end.
 */

#define GATE_US 2000

/* The pin that carries the clock output, and the input of TIM1. */

#define GPIO_MCO                                                               \
    (GPIO_ALT | GPIO_CNF_AFPP | GPIO_MODE_50MHz | GPIO_PORTA | GPIO_PIN8)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Count the edges of one clock on the output pin during the gate, and give
 * the frequency in kHz.
 *
 * Note: TIM1 samples its input at the timer clock of 36 MHz. Thus a source
 * above 18 MHz gives an alias and not its own frequency. The caller reads the
 * known sources as well, and those state where the method stops.
 */

static uint32_t hazk03_mco_khz(uint32_t source) {
    uint32_t start;
    uint32_t now;
    uint32_t count;

    modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM1EN);
    modifyreg32(STM32_RCC_APB1ENR, 0, RCC_APB1ENR_TIM2EN);

    /* Put the source on the clock output, and give that pin to TIM1 as an
     * input. TIM1 drives nothing, thus the output of the clock keeps the pin.
     */

    modifyreg32(STM32_RCC_CFGR, RCC_CFGR_MCO_MASK, source);
    stm32_configgpio(GPIO_MCO);

    /* TIM2 gives the time base of 1 MHz from the 36 MHz of APB1. */

    putreg16(0, STM32_TIM2_CR1);
    putreg16((STM32_APB1_TIM2_CLKIN / 1000000) - 1, STM32_TIM2_PSC);
    putreg16(0xffff, STM32_TIM2_ARR);
    putreg16(ATIM_EGR_UG, STM32_TIM2_EGR);
    putreg16(GTIM_CR1_CEN, STM32_TIM2_CR1);

    /* TIM1 takes its clock from the pin. */

    putreg16(0, STM32_TIM1_CR1);
    putreg16(0, STM32_TIM1_PSC);
    putreg16(0xffff, STM32_TIM1_ARR);
    putreg16(ATIM_CCMR_CCS_CCIN1 << ATIM_CCMR1_CC1S_SHIFT, STM32_TIM1_CCMR1);
    putreg16(0, STM32_TIM1_CCER);
    putreg16(ATIM_SMCR_EXTCLK1 | ATIM_SMCR_TI1FP1, STM32_TIM1_SMCR);
    putreg16(ATIM_EGR_UG, STM32_TIM1_EGR);
    putreg16(0, STM32_TIM1_CNT);
    putreg16(ATIM_CR1_CEN, STM32_TIM1_CR1);

    start = getreg16(STM32_TIM2_CNT);
    do {
        now = getreg16(STM32_TIM2_CNT);
    } while (((now - start) & 0xffff) < GATE_US);

    putreg16(0, STM32_TIM1_CR1);
    putreg16(0, STM32_TIM2_CR1);

    count = getreg16(STM32_TIM1_CNT);

    return count * 1000 / GATE_US;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_hse_probe
 *
 * Description:
 *   Start the external crystal and read its frequency into a text buffer.
 *   The system clock keeps the HSI in every case.
 *
 ****************************************************************************/

void hazk03_hse_probe(char *buf, size_t len) {
    uint32_t spins;

    modifyreg32(STM32_RCC_CR, 0, RCC_CR_HSEON);

    for (spins = 0; spins < HSE_READY_SPINS; spins++) {
        if ((getreg32(STM32_RCC_CR) & RCC_CR_HSERDY) != 0) {
            break;
        }
    }

    if (spins >= HSE_READY_SPINS) {
        modifyreg32(STM32_RCC_CR, RCC_CR_HSEON, 0);
        snprintf(buf, len, "hse absent");
        return;
    }

    /* The HSI of 8 MHz and the PLL of 36 MHz have known frequencies. Thus the
     * two of them state whether the count is correct and where it aliases.
     */

    snprintf(buf, len, "hse ready spins=%lu hsi=%lu pll2=%lu sys=%lu hse=%lu",
             (unsigned long)spins,
             (unsigned long)hazk03_mco_khz(RCC_CFGR_INTCLK),
             (unsigned long)hazk03_mco_khz(RCC_CFGR_PLLCLKd2),
             (unsigned long)hazk03_mco_khz(RCC_CFGR_SYSCLK),
             (unsigned long)hazk03_mco_khz(RCC_CFGR_EXTCLK));
}
