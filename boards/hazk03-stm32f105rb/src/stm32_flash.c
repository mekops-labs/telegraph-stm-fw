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

#include <stddef.h>
#include <string.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/spi/spi.h>

#include <telegraph/ipc.h>

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

/* The store of the settings holds two records. Each record starts at its own
 * erase sector, thus a write to one record never erases the other.
 */

#define CONFIG_RECORDS    2
#define CONFIG_MAGIC      0x484b3031u   /* "HK01" */
#define CONFIG_BLOCKSIZE  256

/* The first block of each erase sector holds one record. */

#define CONFIG_BLOCK(n)   ((n) * W25_BLOCKS_PER_SECTOR)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* One record of the settings. The check value covers every byte before it. */

struct config_record_s
{
  uint32_t magic;
  uint32_t seq;
  struct hazk03_config_s cfg;
  uint16_t crc;
};

/* The check value covers the bytes before it, thus the count comes from the
 * position of that field.
 *
 * Note: the size of the structure is larger, because the compiler puts space
 * after the check value. A count from the size would give a value that covers
 * the check value itself.
 */

#define CONFIG_CRC_BYTES  offsetof(struct config_record_s, crc)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The partition of the settings. The other functions of this file use it. */

static struct mtd_dev_s *g_config_mtd;

/* The record that the board uses, and its sequence number. */

static int      g_config_slot = -1;
static uint32_t g_config_seq;

/* The values in the store. A write with the same values does nothing, thus
 * the flash takes no erase cycle for a value that does not change.
 */

static struct hazk03_config_s g_config_cur;
static bool                   g_config_valid;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Read one record, and give its validity. */

static bool config_read_slot(int slot, struct config_record_s *rec)
{
  uint8_t block[CONFIG_BLOCKSIZE];
  uint16_t crc;
  ssize_t nread;

  nread = MTD_BREAD(g_config_mtd, CONFIG_BLOCK(slot), 1, block);
  if (nread != 1)
    {
      return false;
    }

  memcpy(rec, block, sizeof(*rec));

  if (rec->magic != CONFIG_MAGIC)
    {
      return false;
    }

  crc = ipc_crc16((const uint8_t *)rec, CONFIG_CRC_BYTES);

  return crc == rec->crc;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_config_load
 ****************************************************************************/

int hazk03_config_load(struct hazk03_config_s *cfg)
{
  struct config_record_s rec;
  int slot;
  bool found = false;

  if (g_config_mtd == NULL || cfg == NULL)
    {
      return -ENODEV;
    }

  /* Both records are valid after a normal write. The record with the higher
   * sequence number is the newer one.
   */

  for (slot = 0; slot < CONFIG_RECORDS; slot++)
    {
      if (!config_read_slot(slot, &rec))
        {
          continue;
        }

      if (!found || rec.seq > g_config_seq)
        {
          g_config_seq  = rec.seq;
          g_config_slot = slot;
          *cfg          = rec.cfg;
          found         = true;
        }
    }

  if (found)
    {
      g_config_cur   = *cfg;
      g_config_valid = true;
    }

  return found ? OK : -ENOENT;
}

/****************************************************************************
 * Name: hazk03_config_save
 ****************************************************************************/

int hazk03_config_save(const struct hazk03_config_s *cfg)
{
  uint8_t block[CONFIG_BLOCKSIZE];
  struct config_record_s rec;
  int slot;
  int ret;

  if (g_config_mtd == NULL || cfg == NULL)
    {
      return -ENODEV;
    }

  /* An erase cycle is the cost of a write. Thus the same values give no
   * write, and the flash keeps its life.
   */

  if (g_config_valid && memcmp(&g_config_cur, cfg, sizeof(*cfg)) == 0)
    {
      return OK;
    }

  /* The write goes to the record that the board does not use. Thus a loss of
   * power during the erase or the write keeps the record from before.
   */

  slot = (g_config_slot == 0) ? 1 : 0;

  rec.magic = CONFIG_MAGIC;
  rec.seq   = g_config_seq + 1;
  rec.cfg   = *cfg;
  rec.crc   = ipc_crc16((const uint8_t *)&rec, CONFIG_CRC_BYTES);

  memset(block, 0xff, sizeof(block));
  memcpy(block, &rec, sizeof(rec));

  ret = MTD_ERASE(g_config_mtd, slot, 1);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: config erase %d: %d\n", slot, ret);
      return ret;
    }

  ret = MTD_BWRITE(g_config_mtd, CONFIG_BLOCK(slot), 1, block);
  if (ret != 1)
    {
      syslog(LOG_ERR, "ERROR: config write %d: %d\n", slot, ret);
      return -EIO;
    }

  g_config_slot  = slot;
  g_config_seq   = rec.seq;
  g_config_cur   = *cfg;
  g_config_valid = true;

  return OK;
}

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

  g_config_mtd = part;

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
