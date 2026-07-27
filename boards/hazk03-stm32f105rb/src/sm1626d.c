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

static inline void sm_shiftbit(uint32_t din, bool val)
{
  stm32_gpiowrite(GPIO_SM1626D_CLK, false);
  stm32_gpiowrite(din, val);
  stm32_gpiowrite(GPIO_SM1626D_CLK, true);
}

static inline void sm_latch(void)
{
  stm32_gpiowrite(GPIO_SM1626D_STB, true);
  up_udelay(1);
  stm32_gpiowrite(GPIO_SM1626D_STB, false);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void sm1626d_init(struct sm1626d_dev_s *dev, uint32_t din,
                  uint8_t width, uint8_t height)
{
  dev->bright = SM1626D_BRIGHT_MAX;
  dev->din    = din;
  dev->width  = width;
  dev->height = height;

  memset(dev->fb, 0, sizeof(dev->fb));

  /* The panels stay blank until a scan starts. */

  stm32_gpiowrite(GPIO_SM1626D_OE, true);
  stm32_gpiowrite(GPIO_SM1626D_STB, false);
  stm32_gpiowrite(GPIO_SM1626D_CLK, false);
}

void sm1626d_setbrightness(struct sm1626d_dev_s *dev, uint8_t level)
{
  dev->bright = (level > SM1626D_BRIGHT_MAX) ? SM1626D_BRIGHT_MAX : level;
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
 * Note: this function blanks the panels before it returns. Without that step,
 * the last row stays on until the next scan. The time for one row is only
 * 200 us. Thus a short delay makes that one row much brighter than the
 * others.
 */

void sm1626d_refresh(struct sm1626d_dev_s *dev)
{
  int colbits = SM_COLBITS(dev->width);
  int on_us = (SM_ROW_US * (dev->bright + 1)) / (SM1626D_BRIGHT_MAX + 1);
  int row;
  int col;
  int rbit;

  stm32_gpiowrite(GPIO_SM1626D_OE, false);

  for (row = 0; row < SM1626D_ROWS; row++)
    {
      for (col = colbits - 1; col >= 0; col--)
        {
          bool on = (dev->fb[row][col / 8] & (1 << (col % 8))) != 0;
          sm_shiftbit(dev->din, on);
        }

      /* Select one row. The most significant bit goes first. */

      for (rbit = SM_ROW_SELECT_BITS - 1; rbit >= 0; rbit--)
        {
          sm_shiftbit(dev->din, rbit == row);
        }

      sm_latch();

      /* The output-enable signal gives the brightness. A lower level makes
       * the on-time of each row shorter.
       */

      if (on_us >= SM_ROW_US)
        {
          up_udelay(SM_ROW_US);
        }
      else
        {
          up_udelay(on_us);
          stm32_gpiowrite(GPIO_SM1626D_OE, true);
          up_udelay(SM_ROW_US - on_us);
          stm32_gpiowrite(GPIO_SM1626D_OE, false);
        }
    }

  stm32_gpiowrite(GPIO_SM1626D_OE, true);
}
