/* SPDX-License-Identifier: Apache-2.0 */

/* DS3231 on a bus in software.
 *
 * Note: the pins PC6 and PC7 have no connection to an I2C peripheral on this
 * part. Thus the board gives the pin functions to the generic software bus
 * master. The board then gives that bus to the standard DS3231 driver.
 *
 * Note: this driver gives the time to the system clock. The system reads the
 * RTC at start-up. A call to clock_settime() writes the RTC. The `date -s`
 * command at the shell makes that call.
 *
 * Note: the two lines are open-drain against the pull-up resistors on the
 * board. A high level releases the wire. It does not drive the wire. Thus the
 * device can acknowledge and hold the clock.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <time.h>

#include <nuttx/i2c/i2c_bitbang.h>
#include <nuttx/timers/ds3231.h>

#include "stm32_gpio.h"

#include "hazk03.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void hazk03_i2c_initialize(struct i2c_bitbang_lower_dev_s *lower)
{
  stm32_configgpio(GPIO_DS3231_SCL);
  stm32_configgpio(GPIO_DS3231_SDA);
  stm32_gpiowrite(GPIO_DS3231_SCL, true);
  stm32_gpiowrite(GPIO_DS3231_SDA, true);
}

static void hazk03_i2c_set_scl(struct i2c_bitbang_lower_dev_s *lower,
                               bool high)
{
  stm32_gpiowrite(GPIO_DS3231_SCL, high);
}

static void hazk03_i2c_set_sda(struct i2c_bitbang_lower_dev_s *lower,
                               bool high)
{
  stm32_gpiowrite(GPIO_DS3231_SDA, high);
}

static bool hazk03_i2c_get_scl(struct i2c_bitbang_lower_dev_s *lower)
{
  return stm32_gpioread(GPIO_DS3231_SCL);
}

static bool hazk03_i2c_get_sda(struct i2c_bitbang_lower_dev_s *lower)
{
  return stm32_gpioread(GPIO_DS3231_SDA);
}

static const struct i2c_bitbang_lower_ops_s g_i2c_ops =
{
  .initialize = hazk03_i2c_initialize,
  .set_scl    = hazk03_i2c_set_scl,
  .set_sda    = hazk03_i2c_set_sda,
  .get_scl    = hazk03_i2c_get_scl,
  .get_sda    = hazk03_i2c_get_sda,
};

static struct i2c_bitbang_lower_dev_s g_i2c_lower =
{
  .ops = &g_i2c_ops,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_rtc_initialize
 *
 * Description:
 *   Do nothing.
 *
 *   Note: the clock initialisation calls this function very early. At that
 *   time the software bus is not available. Thus the function
 *   hazk03_rtc_initialize() does the true initialisation later.
 *
 ****************************************************************************/

int up_rtc_initialize(void)
{
  return OK;
}

/****************************************************************************
 * Name: hazk03_rtc_initialize
 *
 * Description:
 *   Start the software bus. Then attach the DS3231 to that bus.
 *
 *   Note: the function returns the bus. The caller uses the bus for the other
 *   registers of the device.
 *
 ****************************************************************************/

struct i2c_master_s *hazk03_rtc_initialize(void)
{
  struct i2c_master_s *i2c;
  int ret;

  i2c = i2c_bitbang_initialize(&g_i2c_lower);
  if (i2c == NULL)
    {
      syslog(LOG_ERR, "ERROR: i2c bitbang init failed\n");
      return NULL;
    }

  ret = dsxxxx_rtc_initialize(i2c);
  syslog(LOG_INFO, "ds3231: init rc=%d\n", ret);
  if (ret < 0)
    {
      return i2c;
    }

  /* Set the system clock from the RTC.
   *
   * Note: without this step, the clock keeps the default value from the
   * build.
   */

  clock_synchronize(NULL);

  {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    syslog(LOG_INFO, "ds3231: synced epoch=%ld\n", (long)ts.tv_sec);
  }

  return i2c;
}
