/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <errno.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/mount.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/usb/usbhost.h>

#include "stm32_otgfs.h"

#include "hazk03.h"

#ifndef CONFIG_STM32_OTGFS
#error "The USB host needs CONFIG_STM32_OTGFS"
#endif

#ifdef CONFIG_USBDEV
#error "The USB host and the USB device cannot share the peripheral"
#endif

#ifdef CONFIG_STM32_OTGFS_VBUS_CONTROL
#error "The board has no switch for VBUS. Refer to STM32_OTGFS_VBUS_CONTROL"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The thread waits on the port. Thus its priority is below the shifter of the
 * rows at 95 and below the server of the protocol at 100.
 */

#define USBHOST_PRIORITY 50
#define USBHOST_STACKSIZE 1536

#ifdef CONFIG_USBHOST_MSC
/* The mass storage class registers its block device during the enumeration,
 * and its geometry arrives from a work queue. Thus the mount takes a few
 * attempts on a slow device.
 */

#define USBHOST_BLOCK "/dev/sda"
#define USBHOST_MOUNT "/media"
#define USBHOST_MOUNT_TRIES 20
#define USBHOST_MOUNT_WAIT_MS 100
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct usbhost_connection_s *g_usbconn;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: usbhost_waiter
 *
 * Description:
 *   Enumerate each device that arrives on the root port. The class driver of
 *   the device then registers its own node under /dev.
 *
 ****************************************************************************/

#ifdef CONFIG_USBHOST_MSC
/****************************************************************************
 * Name: usbhost_mount
 *
 * Description:
 *   Mount the file system of a mass storage device. The edge MCU reaches this
 *   path over the protocol, thus a build with no console still serves the
 *   device.
 *
 ****************************************************************************/

static void usbhost_mount(void) {
    for (int i = 0; i < USBHOST_MOUNT_TRIES; i++) {
        if (mount(USBHOST_BLOCK, USBHOST_MOUNT, "vfat", 0, NULL) == OK) {
            syslog(LOG_INFO, "usbhost: %s holds a file system\n",
                   USBHOST_MOUNT);
            return;
        }

        usleep(USBHOST_MOUNT_WAIT_MS * USEC_PER_MSEC);
    }

    syslog(LOG_ERR, "usbhost: no file system on %s\n", USBHOST_BLOCK);
}
#endif

static int usbhost_waiter(int argc, char *argv[]) {
    for (;;) {
        struct usbhost_hubport_s *hport;

        if (CONN_WAIT(g_usbconn, &hport) < 0) {
            continue;
        }

        if (hport->connected) {
            int ret = CONN_ENUMERATE(g_usbconn, hport);

            syslog(LOG_INFO, "usbhost: port %d gives %d\n", hport->port, ret);

#ifdef CONFIG_USBHOST_MSC
            if (ret == OK) {
                usbhost_mount();
            }
#endif
        } else {
            syslog(LOG_INFO, "usbhost: port %d is empty\n", hport->port);

#ifdef CONFIG_USBHOST_MSC
            umount(USBHOST_MOUNT);
#endif
        }
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_usbhost_initialize
 ****************************************************************************/

int hazk03_usbhost_initialize(void) {
    int ret;

#ifdef CONFIG_USBHOST_MSC
    ret = usbhost_msc_initialize();
    if (ret < 0) {
        syslog(LOG_ERR, "usbhost: mass storage class: %d\n", ret);
        return ret;
    }
#endif

#ifdef CONFIG_USBHOST_CDCACM
    ret = usbhost_cdcacm_initialize();
    if (ret < 0) {
        syslog(LOG_ERR, "usbhost: CDC/ACM class: %d\n", ret);
        return ret;
    }
#endif

    /* The peripheral takes a clock of exactly 48 MHz, thus this call needs the
     * tree of the crystal. Refer to HAZK03_CLOCK_HSE.
     */

    g_usbconn = stm32_otgfshost_initialize(0);
    if (g_usbconn == NULL) {
        syslog(LOG_ERR, "usbhost: no controller\n");
        return -ENODEV;
    }

    ret = kthread_create("usbhost", USBHOST_PRIORITY, USBHOST_STACKSIZE,
                         usbhost_waiter, NULL);
    if (ret < 0) {
        syslog(LOG_ERR, "usbhost: waiter thread: %d\n", ret);
        return ret;
    }

    return OK;
}
