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

#define GPIO_TM1629A_STB                                                       \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTB | GPIO_PIN5)
#define GPIO_TM1629A_CLK                                                       \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTB | GPIO_PIN3)
#define GPIO_TM1629A_DIO                                                       \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTB | GPIO_PIN4)

/* SM1626D dot matrix. Both screens share the clock, the output-enable and the
 * strobe lines. Only the serial data input is different.
 */

#define GPIO_SM1626D_CLK                                                       \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_CLEAR |      \
     GPIO_PORTB | GPIO_PIN12)
#define GPIO_SM1626D_OE                                                        \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTB | GPIO_PIN13)
#define GPIO_SM1626D_STB                                                       \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_CLEAR |      \
     GPIO_PORTB | GPIO_PIN14)
#define GPIO_SM1626D_DIN_MAIN                                                  \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_CLEAR |      \
     GPIO_PORTB | GPIO_PIN15)

/* This is the data input of the sub-screen.
 *
 * Note: after a reset, PA13 is the SWDIO signal. Refer to the function
 * hazk03_jtag_reclaim() in the file stm32_bringup.c.
 */

#define GPIO_SM1626D_DIN_SUB                                                   \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_CLEAR |      \
     GPIO_PORTA | GPIO_PIN13)

/* DS3231 RTC. The pins PC6 and PC7 have no connection to an I2C peripheral on
 * this part. Thus software drives the bus. Both lines are open-drain and use
 * the pull-up resistors on the board.
 */

#define GPIO_DS3231_SCL                                                        \
    (GPIO_OUTPUT | GPIO_CNF_OUTOD | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTC | GPIO_PIN6)
#define GPIO_DS3231_SDA                                                        \
    (GPIO_OUTPUT | GPIO_CNF_OUTOD | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTC | GPIO_PIN7)

/* Winbond W25Q32 serial flash, 4 MB, on SPI1. The peripheral drives PA5, PA6
 * and PA7. The chip-select line is a GPIO, because the driver of the bus
 * controls it for each transfer.
 *
 * Note: the idle level of the chip-select line is high.
 */

#define GPIO_W25_CS                                                            \
    (GPIO_OUTPUT | GPIO_CNF_OUTPP | GPIO_MODE_50MHz | GPIO_OUTPUT_SET |        \
     GPIO_PORTA | GPIO_PIN4)

/* The layout of the flash. One erase sector is 4096 bytes.
 *
 * The first two sectors keep the settings of the board. The sectors that come
 * after them keep the fonts, the icons and the animations.
 *
 * Note: the driver of the flash gives blocks of 256 bytes, thus one erase
 * sector is 16 blocks. The partitions use the block as their unit.
 */

#define W25_BLOCKS_PER_SECTOR 16
#define W25_CONFIG_SECTORS 2
#define W25_TOTAL_SECTORS 1024

#define W25_CONFIG_FIRSTBLOCK 0
#define W25_CONFIG_NBLOCKS (W25_CONFIG_SECTORS * W25_BLOCKS_PER_SECTOR)
#define W25_ASSETS_FIRSTBLOCK W25_CONFIG_NBLOCKS
#define W25_ASSETS_NBLOCKS                                                     \
    ((W25_TOTAL_SECTORS * W25_BLOCKS_PER_SECTOR) - W25_ASSETS_FIRSTBLOCK)

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

/* The places of the assets, and the ending of each kind. A caller names an
 * asset alone, thus it needs neither the place nor the ending.
 */

#define HAZK03_FONT_DIR "/assets/fonts"
#define HAZK03_FONT_EXT ".tgf"
#define HAZK03_ANIM_DIR "/assets/animations"
#define HAZK03_ANIM_EXT ".tgs"

/* The longest name of an asset, without its ending. */

#define HAZK03_ASSET_NAME_MAX 32

#define HAZK03_FONT_PATH HAZK03_FONT_DIR "/default" HAZK03_FONT_EXT

/* The value that turns the sleep period off. */

#define HAZK03_SLEEP_OFF 0xffffu

/* The settings that the board keeps through a loss of power.
 *
 * Note: a new field joins the end of this structure, and it never joins the
 * middle of it. The store then reads a record of an older firmware as the
 * first bytes of this structure, and the new field keeps its default. Thus a
 * step of the firmware loses no setting.
 */

struct hazk03_config_s {
    int16_t utcoffset;  /* Minutes of the local time from UTC          */
    uint8_t digits;     /* Brightness of the digits, 0 is off          */
    uint8_t panels;     /* Brightness of the panels, 0 is off          */
    int16_t tempoffset; /* Correction of the temperature, in tenths    */
    uint16_t sleepmin;  /* Minute of the day that stops the display    */
    uint16_t wakemin;   /* Minute of the day that starts it again      */
};

/* The settings of a board with an empty store. A record that lacks a field
 * takes its value from here.
 *
 * Note: a default of zero is wrong for some fields. The minute that stops the
 * display is one of them, because zero is midnight.
 */

#define HAZK03_CONFIG_DEFAULTS                                                 \
    {                                                                          \
        0,                /* utcoffset  */                                     \
        5,                /* digits     */                                     \
        8,                /* panels     */                                     \
        0,                /* tempoffset */                                     \
        HAZK03_SLEEP_OFF, /* sleepmin   */                                     \
        HAZK03_SLEEP_OFF  /* wakemin    */                                     \
    }

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

/* The longest report of the crystal probe. */

#define HAZK03_HSE_REPORT_MAX 64

/****************************************************************************
 * Name: hazk03_hse_probe
 *
 * Description:
 *   Start the external crystal and write its state and its frequency into a
 *   text buffer.
 *
 *   Note: the system clock keeps the HSI. Thus a crystal that does not start
 *   changes nothing.
 *
 ****************************************************************************/

void hazk03_hse_probe(char *buf, size_t len);

/* The two panels. */

#define HAZK03_PANEL_MAIN 0
#define HAZK03_PANEL_SUB 1

/* The place of a text across a panel. */

#define HAZK03_ALIGN_CENTRE 0
#define HAZK03_ALIGN_LEFT 1
#define HAZK03_ALIGN_RIGHT 2

/* The place of a text down a panel. */

#define HAZK03_VALIGN_MIDDLE 0
#define HAZK03_VALIGN_TOP 1
#define HAZK03_VALIGN_BOTTOM 2

/****************************************************************************
 * Name: hazk03_display_text
 *
 * Description:
 *   Put a text on one panel. An empty text clears that panel.
 *   The text goes in the middle of the panel, at its left or at its right.
 *
 *   Note: the function takes the lock of the framebuffer. Thus a scan pass
 *   never gives a partial image.
 *
 ****************************************************************************/

int hazk03_display_text(int panel, const char *s, size_t len, uint8_t align,
                        uint8_t valign);

/****************************************************************************
 * Name: hazk03_display_pixels
 *
 * Description:
 *   Put a rectangle of pixels on one panel. The rest of that panel keeps its
 *   content, thus two parts of one panel carry two different things.
 *
 *   Note: the bits go row by row, and each row starts at a byte. Bit 7 of a
 *   byte is the pixel at the left.
 *
 ****************************************************************************/

int hazk03_display_pixels(int panel, int x, int y, int w, int h,
                          const uint8_t *bits);

/****************************************************************************
 * Name: hazk03_display_animate
 *
 * Description:
 *   Move a window over a source, inside one rectangle of a panel. A step of
 *   one pixel gives a scroll, and a step of the width of the rectangle gives
 *   the frames of a sprite.
 *
 *   The source is a bitmap when text is false, and a text in UTF-8 when it is
 *   true. A text becomes a bitmap here, thus the edge MCU sends it one time.
 *
 *   Note: a text needs at least 11 rows of height. The cell of the font takes
 *   10 rows for a letter of 7, because a mark such as an acute goes above the
 *   letter. A shorter rectangle loses that mark.
 *
 *   Note: the function gives a negative value when the source does not fit
 *   the space of that panel.
 *
 ****************************************************************************/

/****************************************************************************
 * Name: hazk03_asset_path
 *
 * Description:
 *   Give the full path of an asset from its name alone.
 *
 *   Note: a name that holds a separator gives a negative value. Thus a caller
 *   reaches no file outside the place of that kind.
 *
 ****************************************************************************/

int hazk03_asset_path(char *buf, size_t len, const char *dir, const char *name,
                      size_t namelen, const char *ext);

/****************************************************************************
 * Name: hazk03_asset_list
 *
 * Description:
 *   Put the names of the assets of one kind into a buffer, with a newline
 *   after each one. The ending of each name is removed.
 *
 *   Note: the function gives the bytes that it wrote.
 *
 ****************************************************************************/

size_t hazk03_asset_list(char *buf, size_t len, const char *dir,
                         const char *ext);

int hazk03_display_animate(int panel, int x, int y, int w, int h, bool vertical,
                           uint16_t period_ms, uint8_t step, bool text,
                           bool file, int srcw, int srch, const uint8_t *src,
                           size_t srclen);

/****************************************************************************
 * Name: hazk03_display_animstop
 *
 * Description:
 *   Stop the animation of one panel. The rectangle keeps the pixels of its
 *   last step.
 *
 ****************************************************************************/

void hazk03_display_animstop(int panel);

/****************************************************************************
 * Name: hazk03_display_clear
 *
 * Description:
 *   Take every pixel from one panel.
 *
 *   Note: the animation of that panel stops as well. An animation that kept
 *   its steps would draw over the panel again at its next one.
 *
 ****************************************************************************/

void hazk03_display_clear(int panel);

/****************************************************************************
 * Name: hazk03_display_animspeed
 *
 * Description:
 *   Change the rate of the animation of one panel. A step of 0 keeps the step
 *   that the animation already has.
 *
 *   Note: the animation keeps its source and its place, thus the rate changes
 *   without the cost of sending that source again.
 *
 *   Note: the function gives a negative value when that panel has no
 *   animation.
 *
 ****************************************************************************/

int hazk03_display_animspeed(int panel, uint16_t period_ms, uint8_t step);

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

/****************************************************************************
 * Name: hazk03_usbhost_initialize
 *
 * Description:
 *   Start the USB host on the OTG FS peripheral and register the class
 *   drivers of the supported devices. A thread then enumerates each device
 *   that arrives on the port.
 *
 *   Note: the peripheral takes a clock of exactly 48 MHz, and a PLL on the
 *   crystal is its only source. Refer to HAZK03_CLOCK_HSE.
 *
 *   Note: the pins PA11 and PA12 carry the two data lines. The pins of the
 *   VBUS input and of the OTG identifier carry the UART of the edge MCU, thus
 *   the core takes its VBUS state from an internal source.
 *
 ****************************************************************************/

int hazk03_usbhost_initialize(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H */
