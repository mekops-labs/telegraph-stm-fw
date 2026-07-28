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
 *   Release PA13 for use as the serial data line of the sub-screen.
 *
 *   Note: after a reset, PA13 is the SWDIO signal. The debug port keeps that
 *   pin until this function changes the JTAG and SWD configuration. The GPIO
 *   configuration alone has no effect on this.
 *
 *   Note: after this function runs, a hardware debugger cannot connect. A
 *   reset makes the debug port available again.
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
  /* Do this step before the configuration of PA13 below. */

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

#ifdef CONFIG_MTD_W25
  hazk03_flash_initialize();
#endif

#ifdef CONFIG_NO_SERIAL_CONSOLE
  /* This build has no console. Thus the UART carries the protocol of the
   * edge MCU.
   */

  hazk03_ipc_init();
#endif

#ifdef CONFIG_FS_PROCFS
  int ret = mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: mount /proc failed: %d\n", ret);
    }
#endif

  return OK;
}
