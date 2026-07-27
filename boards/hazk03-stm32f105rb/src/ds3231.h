/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Name: ds3231_temperature
 *
 * Description:
 *   Read the on-die temperature sensor, in tenths of a degree Celsius.
 *   Timekeeping belongs to the stock DS3231 RTC driver; only this register
 *   pair is left to the board.
 *
 ****************************************************************************/

int ds3231_temperature(struct i2c_master_s *i2c, int16_t *out);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H */
