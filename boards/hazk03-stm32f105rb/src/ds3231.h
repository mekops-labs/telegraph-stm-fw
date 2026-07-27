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
 *   Read the temperature sensor. The unit is tenths of a degree Celsius.
 *
 *   Note: the standard DS3231 RTC driver keeps the time and the date. Only
 *   these temperature registers stay with the board.
 *
 ****************************************************************************/

int ds3231_temperature(struct i2c_master_s *i2c, int16_t *out);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_DS3231_H */
