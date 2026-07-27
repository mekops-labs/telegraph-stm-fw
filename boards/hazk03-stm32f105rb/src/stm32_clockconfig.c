/* SPDX-License-Identifier: Apache-2.0 */

/* Clock setup from the HSI oscillator only.
 *
 * Note: the standard connectivity-line code in arch/arm/src/stm32 always
 * drives the main PLL from PREDIV1. It also starts PLL2 and PLL3. All of
 * these use the HSE input.
 *
 * Note: the HSE crystal makes this part stop. Thus this file builds the full
 * clock tree from the internal oscillator.
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

/* These limits stop the wait loops. If a clock does not become ready, the
 * function returns instead of a wait without an end.
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
 *   Set SYSCLK to 36 MHz from the HSI oscillator.
 *
 *   Note: the boot sequence calls this function early. At that time the chip
 *   uses the default HSI clock. This function does not enable the HSE.
 *
 ****************************************************************************/

void stm32_board_clockconfig(void)
{
  uint32_t regval;
  int32_t timeout;

  /* Enable the HSI oscillator.
   *
   * Note: the part boots on the HSI. This step is explicit because the PLL
   * accepts a new configuration only when it is not the system clock.
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

  /* Select the HSI as the system clock. Then stop the PLL. */

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~RCC_CFGR_SW_MASK;
  regval |= RCC_CFGR_SW_HSI;
  putreg32(regval, STM32_RCC_CFGR);

  while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_HSI);

  regval  = getreg32(STM32_RCC_CR);
  regval &= ~RCC_CR_PLLON;
  putreg32(regval, STM32_RCC_CR);

  /* Set the bus prescalers. AHB gets SYSCLK, APB2 gets HCLK, APB1 gets
   * HCLK/2.
   *
   * Note: this step occurs before the increase of SYSCLK. Thus no bus runs
   * above its maximum frequency.
   */

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~(RCC_CFGR_HPRE_MASK | RCC_CFGR_PPRE1_MASK | RCC_CFGR_PPRE2_MASK);
  regval |= (RCC_CFGR_HPRE_SYSCLK | STM32_RCC_CFGR_PPRE1 |
             STM32_RCC_CFGR_PPRE2);
  putreg32(regval, STM32_RCC_CFGR);

  /* Set one flash wait state. This value applies from 24 MHz to 48 MHz. */

  regval  = getreg32(STM32_FLASH_ACR);
  regval &= ~FLASH_ACR_LATENCY_MASK;
  regval |= (FLASH_ACR_LATENCY_1 | FLASH_ACR_PRTFBE);
  putreg32(regval, STM32_FLASH_ACR);

  /* Select the PLL source and the multiplier x9. The result is 36 MHz.
   *
   * Note: a clear PLLSRC bit selects HSI/2. A set bit selects the PREDIV1
   * path, which uses the HSE.
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
      /* If the PLL does not lock, keep the HSI at 8 MHz as the system
       * clock.
       */

      return;
    }

  regval  = getreg32(STM32_RCC_CFGR);
  regval &= ~RCC_CFGR_SW_MASK;
  regval |= RCC_CFGR_SW_PLL;
  putreg32(regval, STM32_RCC_CFGR);

  while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL);
}
