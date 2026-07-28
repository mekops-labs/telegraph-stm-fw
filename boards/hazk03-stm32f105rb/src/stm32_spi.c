/* SPDX-License-Identifier: Apache-2.0 */

/* Board support for SPI1. The bus carries the W25Q32 flash alone.
 *
 * Note: the driver of the bus calls the select function for each transfer.
 * Thus the chip-select line is a GPIO, and not the NSS signal of the
 * peripheral.
 */

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/spi/spi.h>

#include "stm32_gpio.h"
#include "stm32_spi.h"

#include "hazk03.h"

#ifdef CONFIG_STM32_SPI1

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_spidev_initialize
 *
 * Description:
 *   Set the chip-select line to its idle level.
 *
 ****************************************************************************/

void stm32_spidev_initialize(void)
{
  stm32_configgpio(GPIO_W25_CS);
}

/****************************************************************************
 * Name: stm32_spi1select
 *
 * Description:
 *   Drive the chip-select line of one device on the bus.
 *
 *   Note: the line is active low, thus the level is the opposite of the
 *   argument.
 *
 ****************************************************************************/

void stm32_spi1select(struct spi_dev_s *dev, uint32_t devid, bool selected)
{
  if (devid == SPIDEV_FLASH(0))
    {
      stm32_gpiowrite(GPIO_W25_CS, !selected);
    }
}

/****************************************************************************
 * Name: stm32_spi1status
 *
 * Description:
 *   Give the status of one device on the bus. The flash has no status line.
 *
 ****************************************************************************/

uint8_t stm32_spi1status(struct spi_dev_s *dev, uint32_t devid)
{
  return 0;
}

#endif /* CONFIG_STM32_SPI1 */
