/* SPDX-License-Identifier: Apache-2.0 */

/* The W25Q32 serial flash on SPI1.
 *
 * Note: the driver of the part is in the NuttX tree. This file gives only the
 * connection to the bus and the layout of the partitions.
 *
 * Note: the driver reads the identification of the part. Thus a failure here
 * shows a fault of the bus or an absent device.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/spi/spi.h>

#include "stm32_spi.h"

#include "hazk03.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define W25_SPI_BUS       1

#define W25_CONFIG_PATH   "/dev/config"
#define W25_ASSETS_PATH   "/dev/assets"

#define W25_ASSETS_MINOR  0
#define W25_SMART_PATH    "/dev/smart0"
#define W25_ASSETS_MOUNT  "/assets"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_flash_initialize
 ****************************************************************************/

int hazk03_flash_initialize(void)
{
  struct spi_dev_s *spi;
  struct mtd_dev_s *mtd;
  struct mtd_dev_s *part;
  struct mtd_geometry_s geo;
  int ret;

  /* Set the chip-select line before the first transfer. The pin is PA4, which
   * is also the NSS signal of the peripheral. A pin that floats low gives a
   * mode fault, and that fault stops the transfer.
   */

  stm32_spidev_initialize();

  spi = stm32_spibus_initialize(W25_SPI_BUS);
  if (spi == NULL)
    {
      syslog(LOG_ERR, "ERROR: SPI%d not available\n", W25_SPI_BUS);
      return -ENODEV;
    }

  syslog(LOG_INFO, "w25: probe\n");

  /* The driver reads the identification of the part. A failure here shows a
   * fault of the bus, or an absent device.
   */

  mtd = w25_initialize(spi);
  if (mtd == NULL)
    {
      syslog(LOG_ERR, "ERROR: no W25 flash on SPI%d\n", W25_SPI_BUS);
      return -ENODEV;
    }

  ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
  if (ret >= 0)
    {
      syslog(LOG_INFO, "W25: %lu blocks of %lu, erase %lu, total %lu KB\n",
             (unsigned long)geo.neraseblocks, (unsigned long)geo.blocksize,
             (unsigned long)geo.erasesize,
             (unsigned long)((geo.erasesize * geo.neraseblocks) / 1024));
    }

  /* The settings keep their own partition, because they change often and they
   * must survive a loss of power during a write.
   */

  part = mtd_partition(mtd, W25_CONFIG_FIRSTBLOCK, W25_CONFIG_NBLOCKS);
  if (part == NULL)
    {
      syslog(LOG_ERR, "ERROR: config partition failed\n");
      return -EIO;
    }

  ret = register_mtddriver(W25_CONFIG_PATH, part, 0666, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: register %s: %d\n", W25_CONFIG_PATH, ret);
    }

  /* The assets keep a file system, because the edge MCU sends them and their
   * number and their size change.
   */

  part = mtd_partition(mtd, W25_ASSETS_FIRSTBLOCK, W25_ASSETS_NBLOCKS);
  if (part == NULL)
    {
      syslog(LOG_ERR, "ERROR: assets partition failed\n");
      return -EIO;
    }

#ifdef CONFIG_FS_SMARTFS
  ret = smart_initialize(W25_ASSETS_MINOR, part, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: smart_initialize: %d\n", ret);
      return ret;
    }

  ret = mount(W25_SMART_PATH, W25_ASSETS_MOUNT, "smartfs", 0, NULL);
  if (ret < 0)
    {
      /* An unformatted partition gives this result. The command mksmartfs
       * makes the file system.
       */

      syslog(LOG_ERR, "ERROR: mount %s: %d\n", W25_ASSETS_MOUNT, ret);
    }
#else
  ret = register_mtddriver(W25_ASSETS_PATH, part, 0666, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: register %s: %d\n", W25_ASSETS_PATH, ret);
    }
#endif

  return OK;
}
