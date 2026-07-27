/* SPDX-License-Identifier: Apache-2.0 */

/* HSI-only clock setup.
 *
 * The stock connectivity-line path in arch/arm/src/stm32 always drives the
 * main PLL from PREDIV1 and starts PLL2/PLL3, all of which condition the HSE
 * input. Enabling HSE hangs this part, so the whole tree is rebuilt here from
 * the internal oscillator: HSI 8 MHz / 2 -> PLL x9 -> 36 MHz.
 */

#include <nuttx/config.h>

#include <stdint.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_rcc.h"
#include "stm32_flash.h"

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Bounded spin so a clock that never comes up fails fast instead of wedging
 * the boot with no indication of why.
 */

#define HSIRDY_TIMEOUT (1000 * 1000)
#define PLLRDY_TIMEOUT (1000 * 1000)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_board_clockconfig
 *
 * Description:
 *   Bring SYSCLK to 36 MHz from HSI. Called early in the boot sequence with
 *   the chip on its default HSI clock; HSE is never enabled.
 *
 ****************************************************************************/

void stm32_board_clockconfig(void)
{
  uint32_t regval;
  int32_t timeout;

  /* The part boots on HSI, but say so explicitly: the PLL cannot be
   * reconfigured while it is the system clock source.
   */

  regval  = getreg32(STM32_RCC_CR);
  regval |= RCC_CR_HSION;
  putreg32(regval, STM32_RCC_CR);

  for (timeout = HSIRDY_TIMEOUT; timeout > 0; timeout--)
    {
      if ((getreg32(STM32_RCC_CR) & RCC_CR_HSIRDY) != 0)
        {
          break;
        }
    }

  if (timeout <= 0)
    {
      return;
    }

  /* Fall back to HSI as the system clock and stop the PLL before touching
   * its configuration.
   */

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~RCC_CFGR_SW_MASK;
  regval |= RCC_CFGR_SW_HSI;
  putreg32(regval, STM32_RCC_CFGR);

  while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_HSI);

  regval  = getreg32(STM32_RCC_CR);
  regval &= ~RCC_CR_PLLON;
  putreg32(regval, STM32_RCC_CR);

  /* Bus prescalers, set before raising SYSCLK so no bus is ever briefly
   * overclocked: AHB = SYSCLK, APB2 = HCLK, APB1 = HCLK/2.
   */

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~(RCC_CFGR_HPRE_MASK | RCC_CFGR_PPRE1_MASK | RCC_CFGR_PPRE2_MASK);
  regval |= (RCC_CFGR_HPRE_SYSCLK | STM32_RCC_CFGR_PPRE1 |
             STM32_RCC_CFGR_PPRE2);
  putreg32(regval, STM32_RCC_CFGR);

  /* One flash wait state covers 24-48 MHz. */

  regval  = getreg32(STM32_FLASH_ACR);
  regval &= ~FLASH_ACR_LATENCY_MASK;
  regval |= (FLASH_ACR_LATENCY_1 | FLASH_ACR_PRTFBE);
  putreg32(regval, STM32_FLASH_ACR);

  /* PLL source HSI/2 (PLLSRC clear), multiplier x9 -> 36 MHz. Clearing
   * PLLSRC is what selects HSI/2 over the PREDIV1/HSE path.
   */

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMUL_MASK);
  regval |= STM32_PLL_PLLMUL;
  putreg32(regval, STM32_RCC_CFGR);

  regval  = getreg32(STM32_RCC_CR);
  regval |= RCC_CR_PLLON;
  putreg32(regval, STM32_RCC_CR);

  for (timeout = PLLRDY_TIMEOUT; timeout > 0; timeout--)
    {
      if ((getreg32(STM32_RCC_CR) & RCC_CR_PLLRDY) != 0)
        {
          break;
        }
    }

  if (timeout <= 0)
    {
      /* Leave the system running on HSI at 8 MHz rather than switching to a
       * PLL that never locked.
       */

      return;
    }

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~RCC_CFGR_SW_MASK;
  regval |= RCC_CFGR_SW_PLL;
  putreg32(regval, STM32_RCC_CFGR);

  while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL);
}
