/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <debug.h>
#include <sys/mount.h>

#include <nuttx/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_gpio.h"

#include "hazk03.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_jtag_reclaim
 *
 * Description:
 *   Free PA13 for use as the sub-screen serial data line. Out of reset it is
 *   SWDIO, and the debug port keeps it regardless of the GPIO configuration
 *   until the JTAG/SWD remap is applied.
 *
 *   A hardware debugger cannot attach after this runs, until the next reset.
 *
 ****************************************************************************/

static void hazk03_jtag_reclaim(void)
{
  uint32_t regval;

  regval  = getreg32(STM32_AFIO_MAPR);
  regval &= ~AFIO_MAPR_SWJ_CFG_MASK;
  regval |= AFIO_MAPR_DISAB;
  putreg32(regval, STM32_AFIO_MAPR);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_bringup
 ****************************************************************************/

int stm32_bringup(void)
{
  /* Must precede the PA13 configuration below. */

  hazk03_jtag_reclaim();

  stm32_configgpio(GPIO_TM1629A_STB);
  stm32_configgpio(GPIO_TM1629A_CLK);
  stm32_configgpio(GPIO_TM1629A_DIO);

  stm32_configgpio(GPIO_SM1626D_CLK);
  stm32_configgpio(GPIO_SM1626D_OE);
  stm32_configgpio(GPIO_SM1626D_STB);
  stm32_configgpio(GPIO_SM1626D_DIN_MAIN);
  stm32_configgpio(GPIO_SM1626D_DIN_SUB);

  hazk03_display_init();

#ifdef CONFIG_FS_PROCFS
  int ret = mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: mount /proc failed: %d\n", ret);
    }
#endif

  return OK;
}
