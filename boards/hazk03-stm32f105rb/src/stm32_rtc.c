/* SPDX-License-Identifier: Apache-2.0 */

/* DS3231 on a bit-banged bus.
 *
 * PC6/PC7 do not map to an I2C peripheral on this part, so the board supplies
 * the pin half of the generic bit-bang master and hands the resulting bus to
 * the stock DS3231 driver. That driver backs the system clock, so the RTC is
 * read at boot and written by any clock_settime() - `date -s` at the shell.
 *
 * The lines are open-drain against the board pull-ups: driving them high
 * releases the wire rather than forcing it, which is what lets the device
 * acknowledge and clock-stretch.
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
 *   Called from clock initialisation, long before any bus exists. The RTC
 *   sits on a bit-banged bus that is not usable this early, so the real
 *   binding is deferred to hazk03_rtc_initialize() during board bringup.
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
 *   Bring up the bit-banged bus and bind the DS3231 to it. Returns the bus so
 *   the caller can reuse it for the device's non-RTC registers.
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

  /* Seed the system clock from the RTC; without this the clock starts at the
   * build-time default until something sets it.
   */

  clock_synchronize(NULL);

  {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    syslog(LOG_INFO, "ds3231: synced epoch=%ld\n", (long)ts.tv_sec);
  }

  return i2c;
}
