/* SPDX-License-Identifier: Apache-2.0 */

/* DS3231 RTC over a bit-banged bus.
 *
 * PC6/PC7 do not map to an I2C peripheral on this part, so the bus is driven
 * by hand. Both lines are open-drain against the board's pull-ups: writing 1
 * releases a line rather than driving it high, which is what lets the device
 * acknowledge and drive data back on the same wire.
 */

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/arch.h>

#include "stm32_gpio.h"

#include "hazk03.h"
#include "ds3231.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DS3231_ADDR     0x68
#define DS3231_REG_TIME 0x00
#define DS3231_REG_TEMP 0x11

/* Half a bit period; ~100 kHz. */

#define I2C_DELAY_US    5

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void sda_low(void)
{
  stm32_gpiowrite(GPIO_DS3231_SDA, false);
}

static inline void sda_release(void)
{
  stm32_gpiowrite(GPIO_DS3231_SDA, true);
}

static inline bool sda_read(void)
{
  return stm32_gpioread(GPIO_DS3231_SDA);
}

static inline void scl_low(void)
{
  stm32_gpiowrite(GPIO_DS3231_SCL, false);
}

static inline void scl_release(void)
{
  stm32_gpiowrite(GPIO_DS3231_SCL, true);
}

static void i2c_start(void)
{
  sda_release();
  scl_release();
  up_udelay(I2C_DELAY_US);
  sda_low();
  up_udelay(I2C_DELAY_US);
  scl_low();
  up_udelay(I2C_DELAY_US);
}

static void i2c_stop(void)
{
  sda_low();
  up_udelay(I2C_DELAY_US);
  scl_release();
  up_udelay(I2C_DELAY_US);
  sda_release();
  up_udelay(I2C_DELAY_US);
}

/* Returns true if the device acknowledged. */

static bool i2c_write(uint8_t data)
{
  bool ack;
  int i;

  for (i = 0; i < 8; i++)
    {
      if (data & 0x80)
        {
          sda_release();
        }
      else
        {
          sda_low();
        }

      data <<= 1;
      up_udelay(I2C_DELAY_US);
      scl_release();
      up_udelay(I2C_DELAY_US);
      scl_low();
    }

  /* Release SDA so the device can pull it down for the ACK. */

  sda_release();
  up_udelay(I2C_DELAY_US);
  scl_release();
  up_udelay(I2C_DELAY_US);
  ack = !sda_read();
  scl_low();

  return ack;
}

static uint8_t i2c_read(bool ack)
{
  uint8_t data = 0;
  int i;

  sda_release();

  for (i = 0; i < 8; i++)
    {
      data <<= 1;
      up_udelay(I2C_DELAY_US);
      scl_release();
      up_udelay(I2C_DELAY_US);

      if (sda_read())
        {
          data |= 1;
        }

      scl_low();
    }

  if (ack)
    {
      sda_low();
    }
  else
    {
      sda_release();
    }

  up_udelay(I2C_DELAY_US);
  scl_release();
  up_udelay(I2C_DELAY_US);
  scl_low();
  sda_release();

  return data;
}

static uint8_t bcd_to_dec(uint8_t v)
{
  return ((v >> 4) * 10) + (v & 0x0f);
}

/* Point the device's register pointer, then turn the bus around to read. */

static bool ds3231_seek(uint8_t reg)
{
  bool ok;

  i2c_start();
  ok = i2c_write(DS3231_ADDR << 1);
  ok = i2c_write(reg) && ok;
  i2c_stop();

  return ok;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ds3231_read(struct ds3231_time_s *out)
{
  int8_t tmsb;
  uint8_t tlsb;

  if (!ds3231_seek(DS3231_REG_TIME))
    {
      return -EIO;
    }

  i2c_start();
  if (!i2c_write((DS3231_ADDR << 1) | 1))
    {
      i2c_stop();
      return -EIO;
    }

  out->seconds = bcd_to_dec(i2c_read(true) & 0x7f);
  out->minutes = bcd_to_dec(i2c_read(true));
  out->hours   = bcd_to_dec(i2c_read(true) & 0x3f);
  out->wday    = bcd_to_dec(i2c_read(true));
  out->day     = bcd_to_dec(i2c_read(true));
  out->month   = bcd_to_dec(i2c_read(true) & 0x1f);
  out->year    = bcd_to_dec(i2c_read(false));
  i2c_stop();

  if (!ds3231_seek(DS3231_REG_TEMP))
    {
      return -EIO;
    }

  i2c_start();
  if (!i2c_write((DS3231_ADDR << 1) | 1))
    {
      i2c_stop();
      return -EIO;
    }

  tmsb = (int8_t)i2c_read(true);
  tlsb = i2c_read(false);
  i2c_stop();

  /* Upper two bits of the LSB are quarter-degree steps. */

  out->temperature = (int16_t)tmsb * 10 + ((tlsb >> 6) * 10) / 4;

  return OK;
}
