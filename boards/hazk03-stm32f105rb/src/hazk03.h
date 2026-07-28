/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H
#define __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <nuttx/i2c/i2c_master.h>

#include "stm32_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pin map of the display hardware.
 *
 * Note: the TM1629A drives the 7-segment clock digits. It uses a 3-wire
 * serial bus.
 */

#define GPIO_TM1629A_STB  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN5)
#define GPIO_TM1629A_CLK  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN3)
#define GPIO_TM1629A_DIO  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN4)

/* SM1626D dot matrix. Both screens share the clock, the output-enable and the
 * strobe lines. Only the serial data input is different.
 */

#define GPIO_SM1626D_CLK  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTB | GPIO_PIN12)
#define GPIO_SM1626D_OE   (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTB | GPIO_PIN13)
#define GPIO_SM1626D_STB  (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTB | GPIO_PIN14)
#define GPIO_SM1626D_DIN_MAIN \
                          (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTB | GPIO_PIN15)

/* This is the data input of the sub-screen.
 *
 * Note: after a reset, PA13 is the SWDIO signal. Refer to the function
 * hazk03_jtag_reclaim() in the file stm32_bringup.c.
 */

#define GPIO_SM1626D_DIN_SUB \
                          (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_CLEAR | GPIO_PORTA | GPIO_PIN13)

/* DS3231 RTC. The pins PC6 and PC7 have no connection to an I2C peripheral on
 * this part. Thus software drives the bus. Both lines are open-drain and use
 * the pull-up resistors on the board.
 */

#define GPIO_DS3231_SCL   (GPIO_OUTPUT | GPIO_CNF_OUTOD | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTC | GPIO_PIN6)
#define GPIO_DS3231_SDA   (GPIO_OUTPUT | GPIO_CNF_OUTOD | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTC | GPIO_PIN7)

/* Winbond W25Q32 serial flash, 4 MB, on SPI1. The peripheral drives PA5, PA6
 * and PA7. The chip-select line is a GPIO, because the driver of the bus
 * controls it for each transfer.
 *
 * Note: the idle level of the chip-select line is high.
 */

#define GPIO_W25_CS       (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | \
                           GPIO_OUTPUT_SET | GPIO_PORTA | GPIO_PIN4)

/* The layout of the flash. One erase sector is 4096 bytes.
 *
 * The first two sectors keep the settings of the board. The sectors that come
 * after them keep the fonts, the icons and the animations.
 *
 * Note: the driver of the flash gives blocks of 256 bytes, thus one erase
 * sector is 16 blocks. The partitions use the block as their unit.
 */

#define W25_BLOCKS_PER_SECTOR 16
#define W25_CONFIG_SECTORS    2
#define W25_TOTAL_SECTORS     1024

#define W25_CONFIG_FIRSTBLOCK 0
#define W25_CONFIG_NBLOCKS    (W25_CONFIG_SECTORS * W25_BLOCKS_PER_SECTOR)
#define W25_ASSETS_FIRSTBLOCK W25_CONFIG_NBLOCKS
#define W25_ASSETS_NBLOCKS    ((W25_TOTAL_SECTORS * W25_BLOCKS_PER_SECTOR) - \
                               W25_CONFIG_NBLOCKS)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_bringup
 *
 * Description:
 *   Do the initialisation for this board.
 *   Release the debug pins.
 *   Set every display line to a known state.
 *
 ****************************************************************************/

int stm32_bringup(void);

/****************************************************************************
 * Name: hazk03_display_init
 *
 * Description:
 *   Initialise the panels and the digits.
 *   Start the scan loop that keeps the panels on.
 *
 ****************************************************************************/

int hazk03_display_init(void);

/****************************************************************************
 * Name: hazk03_rtc_initialize
 *
 * Description:
 *   Start the software bus.
 *   Attach the DS3231 with the battery to that bus as the system RTC.
 *
 *   Note: the function returns the bus for the other registers of the device.
 *
 ****************************************************************************/

struct i2c_master_s *hazk03_rtc_initialize(void);

/****************************************************************************
 * Name: hazk03_flash_initialize
 *
 * Description:
 *   Start SPI1, and attach the W25Q32 flash to that bus.
 *   Divide the flash into the partition for the settings and the partition
 *   for the assets.
 *
 *   Note: a board without the flash gives an error. The other functions of
 *   the board continue.
 *
 ****************************************************************************/

int hazk03_flash_initialize(void);

/* The value that turns the sleep period off. */

#define HAZK03_SLEEP_OFF  0xffffu

/* The settings that the board keeps through a loss of power. */

struct hazk03_config_s
{
  int16_t  utcoffset;     /* Minutes of the local time from UTC          */
  uint8_t  digits;        /* Brightness of the digits, 0 is off          */
  uint8_t  panels;        /* Brightness of the panels, 0 is off          */
  int16_t  tempoffset;    /* Correction of the temperature, in tenths    */
  uint16_t sleepmin;      /* Minute of the day that stops the display    */
  uint16_t wakemin;       /* Minute of the day that starts it again      */
};

/****************************************************************************
 * Name: hazk03_config_load
 *
 * Description:
 *   Read the settings from the flash.
 *
 *   Note: the function gives -ENOENT if the store holds no valid record. The
 *   caller then keeps its own values.
 *
 ****************************************************************************/

int hazk03_config_load(struct hazk03_config_s *cfg);

/****************************************************************************
 * Name: hazk03_config_save
 *
 * Description:
 *   Write the settings to the flash.
 *
 *   Note: the store holds two records, and a write goes to the record that
 *   the board does not use. Thus a loss of power during a write keeps the
 *   record from before that write.
 *
 ****************************************************************************/

int hazk03_config_save(const struct hazk03_config_s *cfg);

/****************************************************************************
 * Name: hazk03_display_setconfig
 *
 * Description:
 *   Apply the settings from the store to the display.
 *
 *   Note: these values come from the store, thus the function writes nothing
 *   back to it.
 *
 ****************************************************************************/

void hazk03_display_setconfig(const struct hazk03_config_s *cfg);

/****************************************************************************
 * Name: stm32_spidev_initialize
 *
 * Description:
 *   Set the chip-select line of the flash to its idle level.
 *
 ****************************************************************************/

void stm32_spidev_initialize(void);

/* The two panels. */

#define HAZK03_PANEL_MAIN  0
#define HAZK03_PANEL_SUB   1

/****************************************************************************
 * Name: hazk03_display_text
 *
 * Description:
 *   Put a text on one panel. An empty text clears that panel.
 *
 *   Note: the function takes the lock of the framebuffer. Thus a scan pass
 *   never gives a partial image.
 *
 ****************************************************************************/

int hazk03_display_text(int panel, const char *s, size_t len);

/****************************************************************************
 * Name: hazk03_display_temperature
 *
 * Description:
 *   Give the last temperature of the DS3231, in tenths of a degree Celsius.
 *
 ****************************************************************************/

int16_t hazk03_display_temperature(void);

/****************************************************************************
 * Name: hazk03_display_utcoffset
 *
 * Description:
 *   Set the minutes of the local time from UTC. The panels show the local
 *   time, and the RTC keeps UTC.
 *
 *   Note: the board has no store for this value. Thus the edge MCU sends it
 *   again after each reset.
 *
 ****************************************************************************/

void hazk03_display_utcoffset(int16_t minutes);

/****************************************************************************
 * Name: hazk03_display_tempoffset
 *
 * Description:
 *   Set the correction of the temperature, in tenths of a degree Celsius.
 *   The board adds this value to each reading of the DS3231.
 *
 ****************************************************************************/

void hazk03_display_tempoffset(int16_t tenths);

/****************************************************************************
 * Name: hazk03_display_sleep
 *
 * Description:
 *   Set the period that stops the display. Each value is a minute of the
 *   local day. A period that starts after it ends goes through midnight.
 *
 *   Note: the value HAZK03_SLEEP_OFF for the start stops this function. The
 *   display then keeps its brightness through the day.
 *
 *   Note: the period does not change the brightness of the settings. Thus the
 *   display takes its previous levels again at the end of the period.
 *
 ****************************************************************************/

void hazk03_display_sleep(uint16_t sleepmin, uint16_t wakemin);

/****************************************************************************
 * Name: hazk03_display_brightness
 *
 * Description:
 *   Set the brightness of the digits and of the panels. Permitted values are
 *   0 to 8. The value 0 turns the device off. The values 1 to 8 give eight
 *   levels of brightness, from the dimmest to the full level.
 *
 *   Note: the TM1629A has its own control. The panels use the on-time of
 *   each row.
 *
 ****************************************************************************/

int hazk03_display_brightness(uint8_t digits, uint8_t panels);

/****************************************************************************
 * Name: hazk03_ipc_init
 *
 * Description:
 *   Start the task that serves the protocol on the UART of the edge MCU.
 *
 *   Note: this UART also carries the serial console. Thus only a build
 *   without a console starts this task.
 *
 ****************************************************************************/

int hazk03_ipc_init(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H */
