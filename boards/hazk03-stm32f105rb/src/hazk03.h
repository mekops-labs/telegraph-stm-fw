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

/* Initialise this board: release the debug pins, and set every display line
 * to a known state.
 */

int stm32_bringup(void);

/* Initialise the panels and the digits, and start the scan loop that keeps
 * the panels on.
 */

int hazk03_display_init(void);

/* Start the software bus and attach the DS3231 to it as the system RTC. The
 * bus comes back for the registers that driver does not expose.
 */

struct i2c_master_s *hazk03_rtc_initialize(void);

/* Start SPI1, attach the W25Q32, and split it into the settings and the
 * assets. A board with no flash still boots.
 */

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

/* Read the settings from the flash. -ENOENT when the store holds no valid
 * record, and the caller then keeps its own values.
 */

int hazk03_config_load(struct hazk03_config_s *cfg);

/* Write the settings to the flash. The store holds two records and a write
 * goes to the unused one, thus a loss of power keeps the other.
 */

int hazk03_config_save(const struct hazk03_config_s *cfg);

/* Apply the settings of the store to the display, writing nothing back. */

void hazk03_display_setconfig(const struct hazk03_config_s *cfg);

/* Set the chip-select line of the flash to its idle level. */

void stm32_spidev_initialize(void);

/* The longest report of the crystal probe. */

#define HAZK03_HSE_REPORT_MAX 64

/* Start the external crystal and write its state and frequency into a text
 * buffer. The system clock keeps the HSI either way.
 */

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

/* Put a text on one panel, in the middle or at an edge. An empty text clears
 * that panel.
 */

int hazk03_display_text(int panel, const char *s, size_t len, uint8_t align,
                        uint8_t valign);

/* Put a rectangle of pixels on one panel, row by row with bit 7 leftmost. The
 * rest of the panel keeps its content.
 */

int hazk03_display_pixels(int panel, int x, int y, int w, int h,
                          const uint8_t *bits);

/* Move a window over a source inside one rectangle. A step of one pixel
 * scrolls, and a step of the width plays the frames of a sprite.
 */

/* The full path of an asset from its name. Negative for a name holding a
 * separator, thus a caller reaches no file outside the assets.
 */

int hazk03_asset_path(char *buf, size_t len, const char *dir, const char *name,
                      size_t namelen, const char *ext);

/* The names of the assets of one kind into a buffer, one per line and without
 * their ending.
 */

size_t hazk03_asset_list(char *buf, size_t len, const char *dir,
                         const char *ext);

int hazk03_display_animate(int panel, int x, int y, int w, int h, bool vertical,
                           uint16_t period_ms, uint8_t step, bool text,
                           bool file, int srcw, int srch, const uint8_t *src,
                           size_t srclen);

/* Stop the animation of one panel. The rectangle keeps its last step. */

void hazk03_display_animstop(int panel);

/* Take every pixel from one panel. The animation of that panel stops with it,
 * since one that kept its steps would draw over the panel again.
 */

void hazk03_display_clear(int panel);

/* Change the rate of the animation of one panel, keeping its source and its
 * place. A step of 0 keeps the step it has.
 */

int hazk03_display_animspeed(int panel, uint16_t period_ms, uint8_t step);

/* The last temperature of the DS3231, in tenths of a degree Celsius. */

int16_t hazk03_display_temperature(void);

/* The minutes of the local time from UTC. The panels show local time and the
 * RTC keeps UTC; the board has no store for this, thus it is set at each boot.
 */

void hazk03_display_utcoffset(int16_t minutes);

/* The correction of the temperature in tenths of a degree, added to each
 * reading of the DS3231.
 */

void hazk03_display_tempoffset(int16_t tenths);

/* The period that stops the display, as minutes of the local day. A period
 * that starts after it ends goes through midnight.
 */

void hazk03_display_sleep(uint16_t sleepmin, uint16_t wakemin);

/* The brightness of the digits and of the panels, 0 to 8. The value 0 turns a
 * device off, and 1 to 8 are the levels from dimmest to full.
 */

int hazk03_display_brightness(uint8_t digits, uint8_t panels);

/* Start the task serving the protocol on the UART of the edge MCU. That UART
 * also carries the console, thus only a build without one serves it.
 */

int hazk03_ipc_init(void);

/* Start the USB host on OTG FS and register the class drivers. A thread then
 * enumerates each device that arrives.
 */

int hazk03_usbhost_initialize(void);

#endif /* __BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_HAZK03_H */
