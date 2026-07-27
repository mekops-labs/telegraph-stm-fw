/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H

#include <nuttx/config.h>

#include <stdint.h>

struct ds3231_time_s
{
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;        /* 24-hour */
  uint8_t wday;         /* 1 = Monday .. 7 = Sunday */
  uint8_t day;
  uint8_t month;
  uint8_t year;         /* Years since 2000 */
  int16_t temperature;  /* Tenths of a degree Celsius */
};

/****************************************************************************
 * Name: ds3231_read
 *
 * Description:
 *   Read the calendar and the temperature registers. Returns OK, or -EIO if
 *   the device did not acknowledge its address.
 *
 ****************************************************************************/

int ds3231_read(struct ds3231_time_s *out);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H */
