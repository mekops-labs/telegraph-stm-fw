/* SPDX-License-Identifier: Apache-2.0 */

/* Display bring-up: owns the two matrix panels and the 7-segment digits, and
 * runs the scan loop that keeps them lit.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <sched.h>
#include <time.h>

#include <nuttx/arch.h>
#include <nuttx/kthread.h>

#include <nuttx/i2c/i2c_master.h>

#include "hazk03.h"
#include "ds3231.h"
#include "sm1626d.h"
#include "tm1629a.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAIN_W   70
#define MAIN_H   14
#define SUB_W    21
#define SUB_H    14

#define BRIGHTNESS 4

/* Measured at ~13 ms for a pass over both panels, so this ticks the digits
 * about once a second without needing a timer.
 */

#define TICK_EVERY_PASSES 75

/* Anything done between scan passes is time the panels are dark, so the bus
 * read for the temperature is kept rare - it changes far slower than once a
 * second anyway.
 */

#define TEMP_EVERY_TICKS  30

#define DISPLAY_STACKSIZE 1024

/* Below the shell's priority on purpose. The scan loop is a busy wait with
 * nowhere to block, so anything above the shell starves it outright; NSH
 * spends nearly all its time blocked on console input, which leaves the scan
 * loop the CPU it needs.
 */

#define DISPLAY_PRIORITY  90

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sm1626d_dev_s g_main;
static struct sm1626d_dev_s g_sub;
static struct i2c_master_s *g_i2c;
static int16_t g_temp;

/* 21x14 heart, bit 0 = leftmost column. Carried over from the reverse
 * engineered firmware so the sub-screen output is directly comparable.
 */

static const uint32_t g_heart[SUB_H] =
{
  0x000000, 0x01e0f0, 0x03f1f8, 0x07fbfc, 0x07fffc, 0x07fffc, 0x03fff8,
  0x01fff0, 0x00ffe0, 0x007fc0, 0x003f80, 0x001f00, 0x000e00, 0x000400
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* A border plus both diagonals: an error in the column mapping shows up as a
 * broken or stepped diagonal, and the border proves all five shift registers
 * reach their outer columns.
 */

static void draw_geometry_test(struct sm1626d_dev_s *dev)
{
  int x;
  int y;

  sm1626d_clear(dev);

  for (x = 0; x < MAIN_W; x++)
    {
      sm1626d_drawpixel(dev, x, 0, true);
      sm1626d_drawpixel(dev, x, MAIN_H - 1, true);
    }

  for (y = 0; y < MAIN_H; y++)
    {
      sm1626d_drawpixel(dev, 0, y, true);
      sm1626d_drawpixel(dev, MAIN_W - 1, y, true);
    }

  for (x = 0; x < MAIN_W; x++)
    {
      y = (x * (MAIN_H - 1)) / (MAIN_W - 1);
      sm1626d_drawpixel(dev, x, y, true);
      sm1626d_drawpixel(dev, x, MAIN_H - 1 - y, true);
    }
}

static void draw_heart(struct sm1626d_dev_s *dev)
{
  int x;
  int y;

  sm1626d_clear(dev);

  for (y = 0; y < SUB_H; y++)
    {
      for (x = 0; x < SUB_W; x++)
        {
          if (g_heart[y] & (1ul << x))
            {
              sm1626d_drawpixel(dev, x, y, true);
            }
        }
    }
}

static void show_time(const struct tm *t, int16_t temp)
{
  int mon = t->tm_mon + 1;

  tm1629a_setchar(0, (t->tm_hour / 10) ? '0' + t->tm_hour / 10 : ' ');
  tm1629a_setchar(1, '0' + t->tm_hour % 10);
  tm1629a_setchar(2, '0' + t->tm_min / 10);
  tm1629a_setchar(3, '0' + t->tm_min % 10);
  tm1629a_setchar(4, '0' + t->tm_sec / 10);
  tm1629a_setchar(5, '0' + t->tm_sec % 10);

  tm1629a_setchar(6, (mon / 10) ? '0' + mon / 10 : ' ');
  tm1629a_setchar(7, '0' + mon % 10);
  tm1629a_setchar(8, (t->tm_mday / 10) ? '0' + t->tm_mday / 10 : ' ');
  tm1629a_setchar(9, '0' + t->tm_mday % 10);

  tm1629a_setchar(10, '0' + (temp / 100) % 10);
  tm1629a_setchar(11, '0' + (temp / 10) % 10);

  tm1629a_flush();
}

static int display_scanner(int argc, char *argv[])
{
  unsigned int passes = 0;
  unsigned int ticks = TEMP_EVERY_TICKS;

  for (; ; )
    {
      sm1626d_refresh(&g_main);
      sm1626d_refresh(&g_sub);

      if (++passes >= TICK_EVERY_PASSES)
        {
          struct tm tm;
          time_t now;

          passes = 0;

          /* The system clock is backed by the battery-backed DS3231, so this
           * needs no bus traffic; only the temperature does.
           */

          now = time(NULL);
          gmtime_r(&now, &tm);

          if (g_i2c != NULL && ++ticks >= TEMP_EVERY_TICKS)
            {
              ticks = 0;
              ds3231_temperature(g_i2c, &g_temp);
            }

          show_time(&tm, g_temp);
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hazk03_display_init(void)
{
  int ret;

  g_i2c = hazk03_rtc_initialize();

  tm1629a_init(BRIGHTNESS);

  sm1626d_init(&g_main, GPIO_SM1626D_DIN_MAIN, MAIN_W, MAIN_H);
  sm1626d_init(&g_sub, GPIO_SM1626D_DIN_SUB, SUB_W, SUB_H);

  draw_geometry_test(&g_main);
  draw_heart(&g_sub);

  ret = kthread_create("display", DISPLAY_PRIORITY, DISPLAY_STACKSIZE,
                       display_scanner, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: display scanner: %d\n", ret);
      return ret;
    }

  return OK;
}
