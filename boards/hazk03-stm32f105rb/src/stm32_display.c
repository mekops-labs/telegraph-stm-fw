/* SPDX-License-Identifier: Apache-2.0 */

/* Display start-up.
 *
 * Note: this file controls the two matrix panels and the 7-segment digits. It
 * also runs the scan loop that keeps the panels on.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <sched.h>
#include <string.h>
#include <time.h>

#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>

#include <nuttx/i2c/i2c_master.h>

#include "font5x7.h"
#include "hazk03.h"
#include "ds3231.h"
#include "sm1626d.h"
#include "tm1629a.h"
#include "version.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAIN_W   70
#define MAIN_H   14
#define SUB_W    21
#define SUB_H    14

#define BRIGHTNESS 4

/* One pass over both panels takes near 13 ms. Thus this count gives an update
 * of the digits near one time each second. A timer is not necessary.
 */

#define TICK_EVERY_PASSES 75

/* The panels stay dark during all work between two scan passes. Thus the bus
 * read for the temperature occurs rarely. The temperature changes slowly.
 */

#define TEMP_EVERY_TICKS  30

/* The main panel holds the version for this many ticks after a reset.
 *
 * Note: the panel holds 11 characters. A version above that length loses its
 * end.
 */

#define VERSION_TICKS     3

#define DISPLAY_STACKSIZE 1024

/* This priority is less than the priority of the shell.
 *
 * Note: the scan loop is a wait loop. It has no point to stop at. A thread
 * above the shell thus stops the shell fully. The shell waits for console
 * input almost all of the time. Thus the scan loop gets the necessary CPU
 * time.
 */

#define DISPLAY_PRIORITY  90

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sm1626d_dev_s g_main;
static struct sm1626d_dev_s g_sub;
static struct i2c_master_s *g_i2c;
static int16_t g_temp;

/* The scan thread reads the framebuffers. Another thread writes text into
 * them. Thus a lock keeps a scan pass away from a partial image.
 */

static mutex_t g_fblock = NXMUTEX_INITIALIZER;

/* The minutes of the local time from UTC. The RTC keeps UTC, thus this value
 * changes the panels only.
 *
 * Note: the device has no store for this value. Thus the edge MCU sends it
 * again after each reset of this board.
 */

static int16_t g_utcoffset;

/* This is a 21x14 heart image. Bit 0 is the column at the left.
 *
 * Note: this image comes from the reverse engineered firmware. Thus a person
 * can compare the sub-screen output with that firmware.
 */

static const uint32_t g_heart[SUB_H] =
{
  0x000000, 0x01e0f0, 0x03f1f8, 0x07fbfc, 0x07fffc, 0x07fffc, 0x03fff8,
  0x01fff0, 0x00ffe0, 0x007fc0, 0x003f80, 0x001f00, 0x000e00, 0x000400
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Draw a border and the two diagonals.
 *
 * Note: an error in the column calculation gives a broken diagonal. The
 * border shows that all five shift registers reach their outer columns.
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
  unsigned int version_left = VERSION_TICKS;

  for (; ; )
    {
      nxmutex_lock(&g_fblock);
      sm1626d_refresh(&g_main);
      sm1626d_refresh(&g_sub);
      nxmutex_unlock(&g_fblock);

      if (++passes >= TICK_EVERY_PASSES)
        {
          struct tm tm;
          time_t now;

          passes = 0;

          /* The version leaves the panel after its time. */

          if (version_left > 0 && --version_left == 0)
            {
              nxmutex_lock(&g_fblock);
              draw_geometry_test(&g_main);
              nxmutex_unlock(&g_fblock);
            }

          /* Read the system clock. The DS3231 with the battery keeps that
           * clock. Thus this step uses no bus. Only the temperature uses the
           * bus.
           */

          now = time(NULL) + (time_t)g_utcoffset * 60;
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

int hazk03_display_text(int panel, const char *s, size_t len)
{
  struct sm1626d_dev_s *dev = (panel == HAZK03_PANEL_SUB) ? &g_sub : &g_main;

  /* The panel has 14 rows, and the font has 7 rows. Thus this offset puts the
   * text in the middle.
   */

  nxmutex_lock(&g_fblock);
  sm1626d_clear(dev);
  sm1626d_drawtext(dev, 0, (dev->height - FONT5X7_HEIGHT) / 2, s, len);
  nxmutex_unlock(&g_fblock);

  return OK;
}

int hazk03_display_brightness(uint8_t digits, uint8_t panels)
{
  bool digits_on = digits != 0;
  bool panels_on = panels != 0;
  uint8_t digits_level = digits_on ? digits - 1 : 0;
  uint8_t panels_level = panels_on ? panels - 1 : 0;

  tm1629a_setbrightness(digits_level, digits_on);

  nxmutex_lock(&g_fblock);
  sm1626d_setbrightness(&g_main, panels_level, panels_on);
  sm1626d_setbrightness(&g_sub, panels_level, panels_on);
  nxmutex_unlock(&g_fblock);

  return OK;
}

void hazk03_display_utcoffset(int16_t minutes)
{
  g_utcoffset = minutes;
}

int16_t hazk03_display_temperature(void)
{
  return g_temp;
}

int hazk03_display_init(void)
{
  int ret;

  g_i2c = hazk03_rtc_initialize();

  tm1629a_init(BRIGHTNESS);

  sm1626d_init(&g_main, GPIO_SM1626D_DIN_MAIN, MAIN_W, MAIN_H);
  sm1626d_init(&g_sub, GPIO_SM1626D_DIN_SUB, SUB_W, SUB_H);

  /* The version identifies the running image on the panel itself. Thus a
   * person at the bench needs no link to the edge MCU.
   */

  sm1626d_drawtext(&g_main, 0, (MAIN_H - FONT5X7_HEIGHT) / 2,
                   HAZK03_VERSION, strlen(HAZK03_VERSION));
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
