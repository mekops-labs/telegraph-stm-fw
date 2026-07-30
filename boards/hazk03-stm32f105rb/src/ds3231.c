/* SPDX-License-Identifier: Apache-2.0 */

/* DS3231 temperature registers.
 *
 * Note: the standard DS3231 RTC driver keeps the time and the date. That
 * driver gives the values to the system clock.
 *
 * Note: the same driver does not give access to the temperature sensor. Thus
 * the board reads these two registers.
 */

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/i2c/i2c_master.h>

#include "ds3231.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DS3231_ADDR 0x68
#define DS3231_REG_TEMP 0x11
#define DS3231_FREQUENCY 100000

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ds3231_temperature(struct i2c_master_s *i2c, int16_t *out) {
    struct i2c_msg_s msg[2];
    uint8_t reg = DS3231_REG_TEMP;
    uint8_t raw[2];
    int ret;

    msg[0].frequency = DS3231_FREQUENCY;
    msg[0].addr = DS3231_ADDR;
    msg[0].flags = 0;
    msg[0].buffer = &reg;
    msg[0].length = 1;

    msg[1].frequency = DS3231_FREQUENCY;
    msg[1].addr = DS3231_ADDR;
    msg[1].flags = I2C_M_READ;
    msg[1].buffer = raw;
    msg[1].length = sizeof(raw);

    ret = I2C_TRANSFER(i2c, msg, 2);
    if (ret < 0) {
        return ret;
    }

    /* Calculate the temperature in tenths of a degree.
     *
     * Note: the first byte holds the degrees as a signed value. The two upper
     * bits of the second byte are steps of a quarter degree. Thus one step is
     * equal to 2.5 tenths.
     */

    *out = (int16_t)((int8_t)raw[0]) * 10 + (((raw[1] >> 6) * 10) / 4);

    return OK;
}
