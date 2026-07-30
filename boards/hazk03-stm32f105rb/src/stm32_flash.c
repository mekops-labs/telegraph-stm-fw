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
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <syslog.h>

#include <stddef.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/fs/smart.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/spi/spi.h>

#include <telegraph/ipc.h>

#include "stm32_spi.h"

#include "hazk03.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define W25_SPI_BUS 1

#define W25_CONFIG_PATH "/dev/config"
#define W25_ASSETS_PATH "/dev/assets"

#define W25_ASSETS_MINOR 0
#define W25_SMART_PATH "/dev/smart0"
#define W25_ASSETS_MOUNT "/assets"

/* The store of the settings holds two records. Each record starts at its own
 * erase sector, thus a write to one record never erases the other.
 */

#define CONFIG_RECORDS 2

/* This value marks a record of the settings. It never changes. */

#define CONFIG_MAGIC 0x484b4746u /* "FGKH", little-endian "HKGF" */
#define CONFIG_BLOCKSIZE 256

/* The first block of each erase sector holds one record. */

#define CONFIG_BLOCK(n) ((n) * W25_BLOCKS_PER_SECTOR)

/* The version of the layout. A new field takes the next value.
 *
 * Note: the fields of the settings only join the end of the structure. Thus
 * the settings of an older record are the first bytes of the newer structure,
 * and the board reads that record without a loss.
 */

#define CONFIG_VERSION 1u

/* The header of a record:
 *
 *   [magic u32] [sequence u32] [version u16] [length u16] [the settings]
 *
 * The check value comes after the settings, thus its place depends on that
 * length. It covers every byte before it.
 */

#define CONFIG_OFF_MAGIC 0u
#define CONFIG_OFF_SEQ 4u
#define CONFIG_OFF_VERSION 8u
#define CONFIG_OFF_LENGTH 10u
#define CONFIG_OFF_SETTINGS 12u

/* The largest set of settings that one record carries. The value gives room
 * for the fields of the versions that come later.
 */

#define CONFIG_SETTINGS_MAX 64u

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The partition of the settings. The other functions of this file use it. */

static struct mtd_dev_s *g_config_mtd;

/* The record that the board uses, and its sequence number. */

static int g_config_slot = -1;
static uint32_t g_config_seq;

/* The values in the store. A write with the same values does nothing, thus
 * the flash takes no erase cycle for a value that does not change.
 */

static struct hazk03_config_s g_config_cur;
static bool g_config_valid;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Read one record. Give its sequence number and its settings.
 *
 * Note: a record of an older version carries fewer bytes than the structure
 * of this firmware. The settings that it does carry go over the defaults, and
 * the fields that it lacks keep those defaults. Thus a step of the firmware
 * loses no setting.
 */

static bool config_read_slot(int slot, uint32_t *seq,
                             struct hazk03_config_s *cfg) {
    const struct hazk03_config_s defaults = HAZK03_CONFIG_DEFAULTS;
    uint8_t block[CONFIG_BLOCKSIZE];
    uint16_t len;
    uint16_t crc;
    uint16_t stored;

    if (MTD_BREAD(g_config_mtd, CONFIG_BLOCK(slot), 1, block) != 1) {
        return false;
    }

    if (ipc_get_u32(&block[CONFIG_OFF_MAGIC]) != CONFIG_MAGIC) {
        return false;
    }

    len = ipc_get_u16(&block[CONFIG_OFF_LENGTH]);

    /* The length comes from the record, thus a check of it must come before
     * its use. The check value covers it, but only a later step reads that.
     */

    if (len > CONFIG_SETTINGS_MAX ||
        (uint32_t)CONFIG_OFF_SETTINGS + len + sizeof(crc) > CONFIG_BLOCKSIZE) {
        return false;
    }

    crc = ipc_crc16(block, CONFIG_OFF_SETTINGS + len);
    stored = ipc_get_u16(&block[CONFIG_OFF_SETTINGS + len]);

    if (crc != stored) {
        return false;
    }

    if (len > sizeof(*cfg)) {
        /* The record comes from a later firmware. The fields that this build
         * knows are the first ones, thus it takes those and it drops the rest.
         */

        len = (uint16_t)sizeof(*cfg);
    }

    *cfg = defaults;
    memcpy(cfg, &block[CONFIG_OFF_SETTINGS], len);
    *seq = ipc_get_u32(&block[CONFIG_OFF_SEQ]);

    return true;
}

#ifdef CONFIG_FS_SMARTFS
/* Make an empty file system on the partition of the assets.
 *
 * Note: the utility mksmartfs does this work, and it is an application. The
 * configuration for the protocol carries no application, thus this function
 * gives the same steps through the driver alone.
 *
 * Note: a new board reaches this path one time. The partition then holds a
 * file system, and the mount at each start after that one finds it.
 */

static int config_format_assets(void) {
    struct smart_format_s fmt;
    struct smart_read_write_s request;
    uint8_t type = SMARTFS_SECTOR_TYPE_DIR;
    int fd;
    int ret;

    fd = open(W25_SMART_PATH, O_RDWR);
    if (fd < 0) {
        return -ENODEV;
    }

    ret = ioctl(fd, BIOC_LLFORMAT, CONFIG_MTD_SMART_SECTOR_SIZE << 16);
    if (ret < 0) {
        goto done;
    }

    ret = ioctl(fd, BIOC_GETFORMAT, (unsigned long)&fmt);
    if (ret < 0) {
        goto done;
    }

    /* The root directory takes the first sector of the file system. */

    ret = ioctl(fd, BIOC_ALLOCSECT, SMARTFS_ROOT_DIR_SECTOR);
    if (ret != SMARTFS_ROOT_DIR_SECTOR) {
        ret = -EIO;
        goto done;
    }

    request.logsector = SMARTFS_ROOT_DIR_SECTOR;
    request.offset = 0;
    request.count = 1;
    request.buffer = &type;

    ret = ioctl(fd, BIOC_WRITESECT, (unsigned long)&request);

done:
    close(fd);
    return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_config_load
 ****************************************************************************/

int hazk03_config_load(struct hazk03_config_s *cfg) {
    struct hazk03_config_s found_cfg;
    uint32_t seq;
    int slot;
    bool found = false;

    if (g_config_mtd == NULL || cfg == NULL) {
        return -ENODEV;
    }

    /* Both records are valid after a normal write. The record with the higher
     * sequence number is the newer one.
     */

    for (slot = 0; slot < CONFIG_RECORDS; slot++) {
        if (!config_read_slot(slot, &seq, &found_cfg)) {
            continue;
        }

        if (!found || seq > g_config_seq) {
            g_config_seq = seq;
            g_config_slot = slot;
            *cfg = found_cfg;
            found = true;
        }
    }

    if (found) {
        g_config_cur = *cfg;
        g_config_valid = true;
    }

    return found ? OK : -ENOENT;
}

/****************************************************************************
 * Name: hazk03_config_save
 ****************************************************************************/

int hazk03_config_save(const struct hazk03_config_s *cfg) {
    uint8_t block[CONFIG_BLOCKSIZE];
    uint16_t len = (uint16_t)sizeof(*cfg);
    int slot;
    int ret;

    if (g_config_mtd == NULL || cfg == NULL) {
        return -ENODEV;
    }

    /* An erase cycle is the cost of a write. Thus the same values give no
     * write, and the flash keeps its life.
     */

    if (g_config_valid && memcmp(&g_config_cur, cfg, sizeof(*cfg)) == 0) {
        return OK;
    }

    /* The write goes to the record that the board does not use. Thus a loss of
     * power during the erase or the write keeps the record from before.
     */

    slot = (g_config_slot == 0) ? 1 : 0;

    memset(block, 0xff, sizeof(block));

    ipc_put_u32(&block[CONFIG_OFF_MAGIC], CONFIG_MAGIC);
    ipc_put_u32(&block[CONFIG_OFF_SEQ], g_config_seq + 1);
    ipc_put_u16(&block[CONFIG_OFF_VERSION], CONFIG_VERSION);
    ipc_put_u16(&block[CONFIG_OFF_LENGTH], len);
    memcpy(&block[CONFIG_OFF_SETTINGS], cfg, len);

    ipc_put_u16(&block[CONFIG_OFF_SETTINGS + len],
                ipc_crc16(block, CONFIG_OFF_SETTINGS + len));

    ret = MTD_ERASE(g_config_mtd, slot, 1);
    if (ret < 0) {
        syslog(LOG_ERR, "ERROR: config erase %d: %d\n", slot, ret);
        return ret;
    }

    ret = MTD_BWRITE(g_config_mtd, CONFIG_BLOCK(slot), 1, block);
    if (ret != 1) {
        syslog(LOG_ERR, "ERROR: config write %d: %d\n", slot, ret);
        return -EIO;
    }

    g_config_slot = slot;
    g_config_seq = g_config_seq + 1;
    g_config_cur = *cfg;
    g_config_valid = true;

    return OK;
}

/****************************************************************************
 * Name: hazk03_flash_initialize
 ****************************************************************************/

int hazk03_flash_initialize(void) {
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
    if (spi == NULL) {
        syslog(LOG_ERR, "ERROR: SPI%d not available\n", W25_SPI_BUS);
        return -ENODEV;
    }

    syslog(LOG_INFO, "w25: probe\n");

    /* The driver reads the identification of the part. A failure here shows a
     * fault of the bus, or an absent device.
     */

    mtd = w25_initialize(spi);
    if (mtd == NULL) {
        syslog(LOG_ERR, "ERROR: no W25 flash on SPI%d\n", W25_SPI_BUS);
        return -ENODEV;
    }

    ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
    if (ret >= 0) {
        syslog(LOG_INFO, "W25: %lu blocks of %lu, erase %lu, total %lu KB\n",
               (unsigned long)geo.neraseblocks, (unsigned long)geo.blocksize,
               (unsigned long)geo.erasesize,
               (unsigned long)((geo.erasesize * geo.neraseblocks) / 1024));
    }

    /* The settings keep their own partition, because they change often and they
     * must survive a loss of power during a write.
     */

    part = mtd_partition(mtd, W25_CONFIG_FIRSTBLOCK, W25_CONFIG_NBLOCKS);
    if (part == NULL) {
        syslog(LOG_ERR, "ERROR: config partition failed\n");
        return -EIO;
    }

    g_config_mtd = part;

    ret = register_mtddriver(W25_CONFIG_PATH, part, 0666, NULL);
    if (ret < 0) {
        syslog(LOG_ERR, "ERROR: register %s: %d\n", W25_CONFIG_PATH, ret);
    }

    /* The assets keep a file system, because the edge MCU sends them and their
     * number and their size change.
     */

    part = mtd_partition(mtd, W25_ASSETS_FIRSTBLOCK, W25_ASSETS_NBLOCKS);
    if (part == NULL) {
        syslog(LOG_ERR, "ERROR: assets partition failed\n");
        return -EIO;
    }

#ifdef CONFIG_FS_SMARTFS
    ret = smart_initialize(W25_ASSETS_MINOR, part, NULL);
    if (ret < 0) {
        syslog(LOG_ERR, "ERROR: smart_initialize: %d\n", ret);
        return ret;
    }

    ret = mount(W25_SMART_PATH, W25_ASSETS_MOUNT, "smartfs", 0, NULL);
    if (ret < 0) {
        /* A new board carries no file system, thus the mount fails. Make one,
         * and mount it again.
         */

        syslog(LOG_INFO, "assets: no file system, making one\n");

        ret = config_format_assets();
        if (ret < 0) {
            syslog(LOG_ERR, "ERROR: format assets: %d\n", ret);
        } else {
            ret = mount(W25_SMART_PATH, W25_ASSETS_MOUNT, "smartfs", 0, NULL);
            if (ret < 0) {
                syslog(LOG_ERR, "ERROR: mount %s: %d\n", W25_ASSETS_MOUNT, ret);
            }
        }
    }
#endif

    return OK;
}
