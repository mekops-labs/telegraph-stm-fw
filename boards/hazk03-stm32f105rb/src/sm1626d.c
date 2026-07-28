/* SPDX-License-Identifier: Apache-2.0 */

/* SM1626D dot-matrix driver.
 *
 * Two coordinate changes are between the logical pixels and the panel. Keep
 * both of them. An error in one of them gives a mirrored or a moved image.
 *
 * The two coordinate changes are:
 *
 *   - X: the wiring of each 16-bit shift register is in the opposite
 *     direction. Thus the driver turns the position of a pixel in its own
 *     16-column block end for end.
 *   - Y: the sub-screen uses the scan rows 0 and 1. Thus the driver moves
 *     every logical row down by two rows.
 */

#include <nuttx/config.h>

#include <string.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "chip.h"
#include "stm32_gpio.h"

#include "hazk03.h"
#include "sm1626d.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* This is the time for one row. With 16 rows, the frame rate is near 312 Hz.
 * That rate is above the limit for visible flicker.
 */

#define SM_ROW_US       200

#define SM_ROW_SELECT_BITS 16

/* The main screen uses five shift registers. The sub-screen uses two. */

#define SM_COLBITS(w)   (((w) > 32) ? 80 : 32)

/* The on-time of each level, in sixteenths of the row time.
 *
 * Note: the TM1629A gives these same fractions to the digits. Thus one level
 * makes the panels and the digits equally bright. The level 7 is the full row
 * time, because the panels have no other source of light.
 */

/* The transfer of one row uses direct writes to the set-reset register. The
 * function stm32_gpiowrite() is too slow here: it costs near 2 us for each
 * bit, thus a row takes longer than its own on-time.
 *
 * Note: the shared lines are on port B. Only the data input of the sub-screen
 * is on port A.
 */

#define SM_BSRR_SET(pin)  (1ul << (pin))
#define SM_BSRR_CLR(pin)  (1ul << ((pin) + 16))

#define SM_CLK_PIN      12
#define SM_OE_PIN       13
#define SM_STB_PIN      14

#define SM_DUTY_STEPS   16

static const uint8_t g_duty[SM1626D_BRIGHT_MAX + 1] =
{
  1, 2, 4, 10, 11, 12, 13, SM_DUTY_STEPS
};

#define SM_Y_OFFSET     2
#define SM_CHIP_COLS    16

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline int sm_mapx(int x)
{
  int chip = x / SM_CHIP_COLS;
  int local = x % SM_CHIP_COLS;

  return (chip * SM_CHIP_COLS) + (SM_CHIP_COLS - 1 - local);
}

static inline int sm_mapy(int y)
{
  return y + SM_Y_OFFSET;
}

/* The part reads the data at the rising edge of the clock. */

static inline void sm_shiftbit(const struct sm1626d_dev_s *dev, bool val)
{
  putreg32(SM_BSRR_CLR(SM_CLK_PIN), STM32_GPIOB_BSRR);
  putreg32(val ? dev->din_set : dev->din_clr, dev->din_bsrr);
  putreg32(SM_BSRR_SET(SM_CLK_PIN), STM32_GPIOB_BSRR);
}

static inline void sm_latch(void)
{
  putreg32(SM_BSRR_SET(SM_STB_PIN), STM32_GPIOB_BSRR);
  up_udelay(1);
  putreg32(SM_BSRR_CLR(SM_STB_PIN), STM32_GPIOB_BSRR);
}

static inline void sm_output(bool enable)
{
  /* The output-enable signal is active low. */

  putreg32(enable ? SM_BSRR_CLR(SM_OE_PIN) : SM_BSRR_SET(SM_OE_PIN),
           STM32_GPIOB_BSRR);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void sm1626d_init(struct sm1626d_dev_s *dev, uint32_t din,
                  uint8_t width, uint8_t height)
{
  dev->bright = SM1626D_BRIGHT_MAX;
  dev->on = true;

  /* Keep the set-reset register and the masks of the data pin. Thus the
   * transfer loop needs no decode of the pin configuration.
   */

  if ((din & GPIO_PORT_MASK) == GPIO_PORTA)
    {
      dev->din_bsrr = STM32_GPIOA_BSRR;
    }
  else
    {
      dev->din_bsrr = STM32_GPIOB_BSRR;
    }

  dev->din_set = SM_BSRR_SET((din & GPIO_PIN_MASK) >> GPIO_PIN_SHIFT);
  dev->din_clr = SM_BSRR_CLR((din & GPIO_PIN_MASK) >> GPIO_PIN_SHIFT);
  dev->din    = din;
  dev->width  = width;
  dev->height = height;

  memset(dev->fb, 0, sizeof(dev->fb));

  /* The panels stay blank until a scan starts. */

  stm32_gpiowrite(GPIO_SM1626D_OE, true);
  stm32_gpiowrite(GPIO_SM1626D_STB, false);
  stm32_gpiowrite(GPIO_SM1626D_CLK, false);
}

void sm1626d_setbrightness(struct sm1626d_dev_s *dev, uint8_t level, bool on)
{
  dev->bright = (level > SM1626D_BRIGHT_MAX) ? SM1626D_BRIGHT_MAX : level;
  dev->on = on;
}

void sm1626d_clear(struct sm1626d_dev_s *dev)
{
  memset(dev->fb, 0, sizeof(dev->fb));
}

void sm1626d_drawpixel(struct sm1626d_dev_s *dev, int x, int y, bool on)
{
  int px;
  int py;

  if (x < 0 || x >= dev->width || y < 0 || y >= dev->height)
    {
      return;
    }

  px = sm_mapx(x);
  py = sm_mapy(y);

  if (py >= SM1626D_ROWS || (px / 8) >= SM1626D_ROW_BYTES)
    {
      return;
    }

  if (on)
    {
      dev->fb[py][px / 8] |= (1 << (px % 8));
    }
  else
    {
      dev->fb[py][px / 8] &= ~(1 << (px % 8));
    }
}

/* The output-enable signal is active low. Both panels share it.
 *
 * Note: this function blanks the panels at the end of each row. Without that
 * step, the last row stays on until the next scan. The time for one row is
 * only 200 us. Thus a short delay makes that one row much brighter than the
 * others.
 */

void sm1626d_shiftrow(struct sm1626d_dev_s *dev, int row)
{
  int colbits = SM_COLBITS(dev->width);
  int col;
  int rbit;

  for (col = colbits - 1; col >= 0; col--)
    {
      bool on = (dev->fb[row][col / 8] & (1 << (col % 8))) != 0;
      sm_shiftbit(dev, on);
    }

  /* Select one row. The most significant bit goes first. */

  for (rbit = SM_ROW_SELECT_BITS - 1; rbit >= 0; rbit--)
    {
      sm_shiftbit(dev, rbit == row);
    }

  sm_latch();
}

void sm1626d_output(bool enable)
{
  sm_output(enable);
}

int sm1626d_ontime(const struct sm1626d_dev_s *dev, int rowtime_us)
{
  if (!dev->on)
    {
      return 0;
    }

  return (rowtime_us * g_duty[dev->bright]) / SM_DUTY_STEPS;
}

void sm1626d_refresh(struct sm1626d_dev_s *dev)
{
  int colbits;
  int on_us;
  int row;
  int col;
  int rbit;

  if (!dev->on)
    {
      sm_output(false);
      return;
    }

  colbits = SM_COLBITS(dev->width);
  on_us = (SM_ROW_US * g_duty[dev->bright]) / SM_DUTY_STEPS;

  for (row = 0; row < SM1626D_ROWS; row++)
    {
      /* The panel keeps the last row while the driver sends the next one.
       * That time is near the row time itself. Thus the output-enable signal
       * stays off during the transfer, and the brightness is exact.
       */

      sm_output(false);

      for (col = colbits - 1; col >= 0; col--)
        {
          bool on = (dev->fb[row][col / 8] & (1 << (col % 8))) != 0;
          sm_shiftbit(dev, on);
        }

      /* Select one row. The most significant bit goes first. */

      for (rbit = SM_ROW_SELECT_BITS - 1; rbit >= 0; rbit--)
        {
          sm_shiftbit(dev, rbit == row);
        }

      sm_latch();

      /* The on-time of the row gives the brightness. The row time stays the
       * same, thus the frame rate does not change with the level.
       */

      sm_output(true);
      up_udelay(on_us);
      sm_output(false);

      if (on_us < SM_ROW_US)
        {
          up_udelay(SM_ROW_US - on_us);
        }
    }
}
