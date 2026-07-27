/* SPDX-License-Identifier: Apache-2.0 */

/* SM1626D dot-matrix driver.
 *
 * Two coordinate transforms sit between logical pixels and the panel, and both
 * are load-bearing - getting either wrong produces a mirrored or shifted
 * image:
 *
 *   X: the 16-bit shift registers are wired in reverse, so a pixel's position
 *      within its own 16-column block is flipped end for end.
 *   Y: the sub-screen occupies scan rows 0 and 1, so every logical row is
 *      pushed down by two.
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

/* Row dwell time. 16 rows at this interval gives a ~312 Hz frame rate, above
 * the flicker threshold.
 */

#define SM_ROW_US       200

#define SM_ROW_SELECT_BITS 16

/* The main screen spans five shift registers, the sub-screen two. */

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

/* Data is sampled on the rising edge. */

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
  dev->din    = din;
  dev->width  = width;
  dev->height = height;

  memset(dev->fb, 0, sizeof(dev->fb));

  /* OE is active low: drive it low to enable the panel. */

  stm32_gpiowrite(GPIO_SM1626D_OE, false);
  stm32_gpiowrite(GPIO_SM1626D_STB, false);
  stm32_gpiowrite(GPIO_SM1626D_CLK, false);
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

void sm1626d_refresh(struct sm1626d_dev_s *dev)
{
  int colbits = SM_COLBITS(dev->width);
  int row;
  int col;
  int rbit;

  for (row = 0; row < SM1626D_ROWS; row++)
    {
      for (col = colbits - 1; col >= 0; col--)
        {
          bool on = (dev->fb[row][col / 8] & (1 << (col % 8))) != 0;
          sm_shiftbit(dev->din, on);
        }

      /* One-hot row select, most significant bit first. */

      for (rbit = SM_ROW_SELECT_BITS - 1; rbit >= 0; rbit--)
        {
          sm_shiftbit(dev->din, rbit == row);
        }

      sm_latch();
      up_udelay(SM_ROW_US);
    }
}
