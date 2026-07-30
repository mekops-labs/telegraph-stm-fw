/* SPDX-License-Identifier: Apache-2.0 */

/* TM1629A 7-segment driver. The interface is a 3-wire serial bus with STB,
 * CLK and DIO. The bus sends the least significant bit first.
 *
 * Note: the wiring puts the segments of one digit at different controller
 * addresses. Segment N of every digit uses address N*2. Digits in the upper
 * bit half add 1 to that address. The digit selects one bit at the address.
 *
 * Note: the function tm1629a_setraw() does this distribution. The framebuffer
 * holds a copy of the controller memory. The flush function writes all of it.
 */

#include <nuttx/config.h>

#include <string.h>

#include <nuttx/arch.h>

#include "stm32_gpio.h"

#include "hazk03.h"
#include "tm1629a.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TM_FB_LEN 16

#define TM_CMD_DATA 0x40  /* Write display data, auto-increment address */
#define TM_CMD_ADDR0 0xc0 /* Set address 0 */
#define TM_CMD_DISP_OFF 0x80
#define TM_CMD_DISP_ON 0x88 /* Or'd with brightness 0-7 */

/* The maximum bus frequency of this part is 1 MHz.
 *
 * Note: direct register writes are much faster than this limit. Thus the code
 * puts a delay between each edge.
 */

#define TM_EDGE_US 1

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_fb[TM_FB_LEN];
static uint8_t g_brightness = 7;
static bool g_display_on = true;

/* This table changes a logical digit into a controller bit index.
 *
 * Note: the last two entries are not adjacent to the others. Bits 10 and 11
 * drive decorative LEDs, not digits.
 */

static const uint8_t g_digit_to_bit[TM1629A_NDIGITS] = {0, 1, 2, 3, 4,  5,
                                                        6, 7, 8, 9, 12, 13};

/* Bit 0 is segment A. Bit 6 is segment G. Index 16 gives a blank digit. */

static const uint8_t g_font[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, /* 0-7 */
    0x7f, 0x6f,                                     /* 8-9 */
    0x77,                                           /* A */
    0x7c,                                           /* b */
    0x39,                                           /* C */
    0x5e,                                           /* d */
    0x79,                                           /* E */
    0x71,                                           /* F */
    0x00                                            /* blank */
};

#define TM_FONT_BLANK 16

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tm_write_byte(uint8_t val) {
    int i;

    for (i = 0; i < 8; i++) {
        stm32_gpiowrite(GPIO_TM1629A_DIO, (val & 1) != 0);
        val >>= 1;
        up_udelay(TM_EDGE_US);

        /* The part reads the data at the rising edge of the clock. */

        stm32_gpiowrite(GPIO_TM1629A_CLK, true);
        up_udelay(TM_EDGE_US);
        stm32_gpiowrite(GPIO_TM1629A_CLK, false);
    }
}

static void tm_command(uint8_t cmd) {
    stm32_gpiowrite(GPIO_TM1629A_STB, false);
    tm_write_byte(cmd);
    stm32_gpiowrite(GPIO_TM1629A_STB, true);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void tm1629a_setbrightness(uint8_t level, bool on) {
    if (level > 7) {
        level = 7;
    }

    g_brightness = level;
    g_display_on = on;

    tm_command(on ? (TM_CMD_DISP_ON | level) : TM_CMD_DISP_OFF);
}

void tm1629a_init(uint8_t brightness) {
    stm32_gpiowrite(GPIO_TM1629A_STB, true);
    stm32_gpiowrite(GPIO_TM1629A_CLK, true);

    g_brightness = brightness & 0x07;
    g_display_on = true;

    memset(g_fb, 0, sizeof(g_fb));
    tm1629a_flush();
}

void tm1629a_flush(void) {
    int i;

    tm_command(TM_CMD_DATA);

    stm32_gpiowrite(GPIO_TM1629A_STB, false);
    tm_write_byte(TM_CMD_ADDR0);
    for (i = 0; i < TM_FB_LEN; i++) {
        tm_write_byte(g_fb[i]);
    }

    stm32_gpiowrite(GPIO_TM1629A_STB, true);

    tm1629a_setbrightness(g_brightness, g_display_on);
}

void tm1629a_clear(void) {
    memset(g_fb, 0, sizeof(g_fb));
    tm1629a_flush();
}

void tm1629a_setraw(uint8_t digit, uint8_t segments) {
    uint8_t bitidx;
    uint8_t offset;
    uint8_t mask;
    int seg;

    if (digit >= TM1629A_NDIGITS) {
        return;
    }

    bitidx = g_digit_to_bit[digit];
    offset = (bitidx >= 8) ? 1 : 0;
    mask = 1 << (bitidx % 8);

    for (seg = 0; seg < 7; seg++) {
        uint8_t addr = (seg * 2) + offset;

        if (segments & (1 << seg)) {
            g_fb[addr] |= mask;
        } else {
            g_fb[addr] &= ~mask;
        }
    }
}

void tm1629a_setchar(uint8_t digit, char c) {
    uint8_t glyph;

    if (c >= '0' && c <= '9') {
        glyph = g_font[c - '0'];
    } else if (c >= 'A' && c <= 'F') {
        glyph = g_font[10 + (c - 'A')];
    } else if (c >= 'a' && c <= 'f') {
        glyph = g_font[10 + (c - 'a')];
    } else if (c == ' ') {
        glyph = g_font[TM_FONT_BLANK];
    } else {
        return;
    }

    tm1629a_setraw(digit, glyph);
}
