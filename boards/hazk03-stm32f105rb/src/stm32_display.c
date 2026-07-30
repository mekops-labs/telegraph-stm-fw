/* SPDX-License-Identifier: Apache-2.0 */

/* Display start-up.
 *
 * Note: this file controls the two matrix panels and the 7-segment digits. It
 * also runs the scan loop that keeps the panels on.
 */

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <time.h>

#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include "stm32_tim.h"

#include <fcntl.h>
#include <unistd.h>

#include <telegraph/ipc.h>

#include <nuttx/i2c/i2c_master.h>

#include "font5x7.h"
#include "fontext.h"
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

/* The time of one row. Both panels take the same row in one pass, thus one
 * full image takes 16 of these.
 *
 * Note: the rate of the row events is the limit of this board, and not the
 * work in each one. Above near 1250 events each second the UART loses bytes
 * at 460800 baud, because an interrupt and a change of thread each hold the
 * other interrupts for a time.
 */

#define ROW_US            800

/* The transfer of one row goes into this many parts, and the timer gives one
 * interrupt for each part. A part must be shorter than one byte of the UART,
 * which is 21.7 us at 460800 baud. Thus the link keeps its bytes.
 */

/* The thread that sends the rows runs BELOW the task of the protocol.
 *
 * Note: the opposite order loses frames of the protocol. The task of the
 * protocol then waits behind every row, its receive buffer fills, and the
 * bytes of a burst go past its end. Measured with the rows above that task,
 * a burst of 200 frames delivered none of them.
 *
 * Note: the cost of this order is a row that comes late while the protocol
 * holds the CPU. That shows as a short flicker, and a lost frame does not
 * repair itself.
 */

#define SHIFT_PRIORITY    95
#define SHIFT_STACKSIZE   1024

/* The timer that paces the rows. TIM3 has no other user on this board. */

#define DISPLAY_TIMER     3

/* The thread of the board runs at this period. It moves the animations, and
 * it keeps the digits when the second of the clock changes.
 *
 * Note: the digits follow the clock and not a count of these waits. A wait
 * takes at least its period and often more, thus a count of them drifts.
 */

#define TICK_MS           20
#define TICK_US           (TICK_MS * 1000)

/* The source of an animation. The main panel holds a message wider than
 * itself, and the sub panel holds less.
 */

#define ANIM_SRC_MAIN     512
#define ANIM_SRC_SUB      128

/* A SET_TEXT frame that overflows its panel scrolls at this speed. A
 * SET_ANIM call on the same panel may set a different speed instead.
 */

#define IPC_TEXT_SCROLL_PERIOD_MS 60
#define IPC_TEXT_SCROLL_STEP      1

/* The blank gap after a scrolling text, before it wraps back to its start,
 * in units of the font's advance width.
 */

#define TEXT_SCROLL_GAP_CHARS     3

/* The most lines a wrapped text takes, bounded so that its rendered bitmap
 * never exceeds ANIM_SRC_MAIN: at a 70-px panel and this font's shortest
 * possible line height (7 rows, the compact font), 7 lines plus one gap
 * line is 504 of the 512 bytes the source buffer holds.
 */

#define TEXT_WRAP_MAX_LINES       7

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

/* The timer that paces the rows, the row that comes next, and the count of
 * the images. The interrupt owns these.
 */

static struct stm32_tim_dev_s *g_tim;
static volatile int      g_slot;

/* The interrupt of the row wakes the thread that sends the bits. */

static sem_t g_rowsem = SEM_INITIALIZER(0);
static volatile uint32_t g_frames;

/* The minutes of the local time from UTC. The RTC keeps UTC, thus this value
 * changes the panels only.
 *
 * Note: the device has no store for this value. Thus the edge MCU sends it
 * again after each reset of this board.
 */

static int16_t g_utcoffset;

/* The levels that the board keeps in the flash with the offset above. */

static uint8_t g_bright_digits = BRIGHTNESS;
static uint8_t g_bright_panels = BRIGHTNESS;

/* The correction of the temperature, in tenths of a degree Celsius. */

static int16_t g_tempoffset;

/* The period that stops the display, in minutes of the local day. */

static uint16_t g_sleepmin = HAZK03_SLEEP_OFF;
static uint16_t g_wakemin  = HAZK03_SLEEP_OFF;

/* The display is in the period above. The levels of the settings stay, thus
 * the display takes them again at the end of the period.
 */

static bool g_asleep;

/* The panels carry their own content until the edge MCU sends a text. Thus a
 * board with no link still gives a greeting and the day of the week.
 */

static bool g_main_default = true;
static bool g_sub_default  = true;

/* The day that the sub panel carries. The panel takes a new text when the day
 * changes, and not at each tick.
 */

static int g_sub_wday = -1;

/* The greeting of the main panel, in Polish. The text is in UTF-8, thus the
 * extended font from the flash gives the letter with the acute. */

#define GREETING  "Dzień dobry"

/* The days of the week in Polish, from Sunday. The sub panel holds three
 * characters.
 */

static const char *g_weekday[] =
{
  "Nie", "Pon", "Wto", "Śro", "Czw", "Pią", "Sob"
};

/* One animation for each panel. The window moves over the source, thus a step
 * of one pixel scrolls and a step of the width gives the frames of a sprite.
 */

struct display_anim_s
{
  bool     active;
  bool     vertical;
  uint8_t  x;
  uint8_t  y;
  uint8_t  w;
  uint8_t  h;
  uint16_t period_ms;
  uint8_t  step;
  uint16_t srcw;
  uint16_t srch;
  uint16_t offset;
  uint16_t elapsed_ms;
  uint8_t *src;
};

static uint8_t g_anim_src_main[ANIM_SRC_MAIN];
static uint8_t g_anim_src_sub[ANIM_SRC_SUB];

static struct display_anim_s g_anim[2];

/* Put the settings into the flash. The store makes no write if the values are
 * the same as the values that it holds.
 */

static void display_persist(void)
{
#ifdef CONFIG_MTD_W25
  struct hazk03_config_s cfg;

  cfg.utcoffset  = g_utcoffset;
  cfg.digits     = g_bright_digits;
  cfg.panels     = g_bright_panels;
  cfg.tempoffset = g_tempoffset;
  cfg.sleepmin   = g_sleepmin;
  cfg.wakemin    = g_wakemin;

  hazk03_config_save(&cfg);
#endif
}

/* Put the levels on the hardware. This function changes no setting, thus the
 * sleep period uses it to stop the display without a loss of the levels.
 */

static void display_apply_brightness(uint8_t digits, uint8_t panels)
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
}

/* Give the state of the sleep period for one minute of the local day. */

static bool display_in_sleep(uint16_t minute)
{
  if (g_sleepmin == HAZK03_SLEEP_OFF || g_wakemin == HAZK03_SLEEP_OFF ||
      g_sleepmin == g_wakemin)
    {
      return false;
    }

  /* A period that starts after it ends goes through midnight. */

  if (g_sleepmin < g_wakemin)
    {
      return minute >= g_sleepmin && minute < g_wakemin;
    }

  return minute >= g_sleepmin || minute < g_wakemin;
}

/* Stop or start the display at the edges of the sleep period. */

static void display_update_sleep(const struct tm *t)
{
  uint16_t minute = (uint16_t)(t->tm_hour * 60 + t->tm_min);
  bool sleeping = display_in_sleep(minute);

  if (sleeping == g_asleep)
    {
      return;
    }

  g_asleep = sleeping;

  if (sleeping)
    {
      display_apply_brightness(0, 0);
    }
  else
    {
      display_apply_brightness(g_bright_digits, g_bright_panels);
    }
}

/* Give the first column of a text on one panel.
 *
 * Note: a text that is wider than the panel starts at the left edge. Thus its
 * end goes past that panel, and its start stays visible.
 */

static int text_column(struct sm1626d_dev_s *dev, const char *s, size_t len,
                       uint8_t align)
{
  int space = dev->width - sm1626d_textwidth(s, len);

  if (space <= 0)
    {
      return 0;
    }

  switch (align)
    {
      case HAZK03_ALIGN_LEFT:
        return 0;

      case HAZK03_ALIGN_RIGHT:
        return space;

      default:
        return space / 2;
    }
}

/* Put a text on one panel, and take the lock of the framebuffer. */

/* Give the top row of a line of text on one panel. */

static int text_row(struct sm1626d_dev_s *dev, uint8_t valign)
{
  int line = fontext_lineheight();

  switch (valign)
    {
      case HAZK03_VALIGN_TOP:
        return fontext_ascent();

      case HAZK03_VALIGN_BOTTOM:
        return dev->height - line + fontext_ascent();

      default:
        return ((dev->height - line) / 2) + fontext_ascent();
    }
}

static void draw_text(struct sm1626d_dev_s *dev, const char *s, size_t len,
                      uint8_t align, uint8_t valign)
{
  int x = text_column(dev, s, len, align);
  int y = text_row(dev, valign);
  int line = fontext_lineheight();
  int row;
  int col;

  nxmutex_lock(&g_fblock);
  sm1626d_begin(dev);

  /* Take the rows of this line alone. Thus a text on the top leaves a text on
   * the bottom where it is.
   */

  for (row = y - fontext_ascent(); row < y - fontext_ascent() + line; row++)
    {
      for (col = 0; col < dev->width; col++)
        {
          sm1626d_drawpixel(dev, col, row, false);
        }
    }

  sm1626d_drawtext(dev, x, y, s, len);
  sm1626d_commit(dev);
  nxmutex_unlock(&g_fblock);
}

/* Put one step of an animation on its rectangle.
 *
 * Note: the window takes its pixels from the source and returns to the start
 * at the end of that source. Thus the message repeats without a gap in the
 * code that moves it.
 */

static void anim_draw(struct sm1626d_dev_s *dev, struct display_anim_s *a)
{
  int stride = (a->srcw + 7) / 8;
  int row;
  int col;

  sm1626d_begin(dev);

  for (row = 0; row < a->h; row++)
    {
      for (col = 0; col < a->w; col++)
        {
          int sx = col;
          int sy = row;
          bool on;

          if (a->vertical)
            {
              sy = (row + a->offset) % a->srch;
            }
          else
            {
              sx = (col + a->offset) % a->srcw;
            }

          if (sx >= a->srcw || sy >= a->srch)
            {
              on = false;
            }
          else
            {
              on = (a->src[(sy * stride) + (sx / 8)] &
                    (0x80 >> (sx % 8))) != 0;
            }

          sm1626d_drawpixel(dev, a->x + col, a->y + row, on);
        }
    }

  sm1626d_commit(dev);
}

/* Move every animation that is due. */

static void anim_tick(void)
{
  int panel;

  for (panel = 0; panel < 2; panel++)
    {
      struct display_anim_s *a = &g_anim[panel];
      struct sm1626d_dev_s *dev;

      if (!a->active)
        {
          continue;
        }

      a->elapsed_ms += TICK_MS;

      if (a->elapsed_ms < a->period_ms)
        {
          continue;
        }

      a->elapsed_ms = 0;
      a->offset += a->step;

      if (a->vertical)
        {
          a->offset %= (a->srch > 0) ? a->srch : 1;
        }
      else
        {
          a->offset %= (a->srcw > 0) ? a->srcw : 1;
        }

      dev = (panel == HAZK03_PANEL_SUB) ? &g_sub : &g_main;

      nxmutex_lock(&g_fblock);
      anim_draw(dev, a);
      nxmutex_unlock(&g_fblock);
    }
}

/* Break s into lines of at most maxw pixels, breaking at a space where one
 * is available. A single word wider than maxw breaks mid-word, at the last
 * character that still fits. Returns the line count, at most maxlines.
 *
 * Note: a caller that finds the returned count below the count the text
 * needs has lost the remainder of the text, the same way a too-long source
 * elsewhere in this file is capped rather than rejected.
 */

static int wrap_text(const char *s, size_t len, int maxw, size_t *starts,
                     size_t *lens, int maxlines)
{
  size_t i = 0;
  int n = 0;

  while (i < len && n < maxlines)
    {
      size_t linestart = i;
      size_t lastspace = 0;
      bool hasspace = false;
      size_t j = i;

      while (j < len)
        {
          const uint16_t *cols;
          size_t next = j + fontext_next(&s[j], len - j, &cols);

          if (sm1626d_textwidth(&s[linestart], next - linestart) > maxw)
            {
              break;
            }

          if (s[j] == ' ')
            {
              lastspace = j;
              hasspace = true;
            }

          j = next;
        }

      if (j >= len)
        {
          starts[n] = linestart;
          lens[n] = len - linestart;
          n++;
          break;
        }

      if (hasspace && lastspace > linestart)
        {
          starts[n] = linestart;
          lens[n] = lastspace - linestart;
          i = lastspace + 1;
        }
      else
        {
          starts[n] = linestart;
          lens[n] = j - linestart;
          i = j;
        }

      n++;
    }

  return n;
}

/* Draw one already-wrapped line directly onto a panel, clearing its own rows
 * first. draw_text() does the same for a single centred line; this is the
 * per-line version a multi-line static draw calls once per line.
 */

static void draw_line_at(struct sm1626d_dev_s *dev, const char *s, size_t len,
                         uint8_t align, int ytop)
{
  int x = text_column(dev, s, len, align);
  int line = fontext_lineheight();
  int row;
  int col;

  for (row = ytop; row < ytop + line; row++)
    {
      for (col = 0; col < dev->width; col++)
        {
          sm1626d_drawpixel(dev, col, row, false);
        }
    }

  sm1626d_drawtext(dev, x, ytop + fontext_ascent(), s, len);
}

/* Draw a text on one panel, or start it scrolling if the text does not fit.
 *
 * A caller that wants the fixed truncating draw calls draw_text() directly.
 * A board default and an IPC text both go through this one instead, so a
 * long boot version scrolls the same way a long IPC text does.
 */

static void draw_text_or_scroll(struct sm1626d_dev_s *dev, const char *s,
                                size_t len, uint8_t align, uint8_t valign)
{
  int width = sm1626d_textwidth(s, len);

  if (width > dev->width)
    {
      int panel = (dev == &g_sub) ? HAZK03_PANEL_SUB : HAZK03_PANEL_MAIN;
      int line = fontext_lineheight();
      int maxlines = dev->height / line;

      /* The font's own line height decides whether a second line fits at
       * all. The default font's 10 rows leave no room for one on a 14-row
       * panel; the compact font's 7 rows leave room for two.
       */

      if (maxlines > 1)
        {
          size_t starts[TEXT_WRAP_MAX_LINES];
          size_t lens[TEXT_WRAP_MAX_LINES];
          int n = wrap_text(s, len, dev->width, starts, lens,
                            TEXT_WRAP_MAX_LINES);

          if (n > 1)
            {
              int total = n * line;

              if (total <= dev->height)
                {
                  int voff = (dev->height - total) / 2;
                  int k;

                  nxmutex_lock(&g_fblock);
                  sm1626d_begin(dev);

                  for (k = 0; k < n; k++)
                    {
                      draw_line_at(dev, &s[starts[k]], lens[k], align,
                                  voff + k * line);
                    }

                  sm1626d_commit(dev);
                  nxmutex_unlock(&g_fblock);
                  return;
                }

              /* The wrapped block is taller than the panel. Render it once
               * into a scratch bitmap and scroll it down by rows, the same
               * window/source mechanism the horizontal scroll uses.
               */

              {
                static uint8_t scratch[ANIM_SRC_MAIN];
                int xoffs[TEXT_WRAP_MAX_LINES];
                int gap = line;
                int srch = total + gap;
                int stride = (dev->width + 7) / 8;
                int ret;
                int k;

                for (k = 0; k < n; k++)
                  {
                    xoffs[k] = text_column(dev, &s[starts[k]], lens[k],
                                           align);
                  }

                sm1626d_rendertextlines(scratch, dev->width, srch, starts,
                                        lens, xoffs, n, s);
                ret = hazk03_display_animate(
                    panel, 0, 0, dev->width, dev->height, true,
                    IPC_TEXT_SCROLL_PERIOD_MS, IPC_TEXT_SCROLL_STEP, false,
                    false, dev->width, srch, scratch,
                    (size_t)(stride * srch));

                if (ret == OK)
                  {
                    return;
                  }

                /* The wrapped block did not fit the source buffer of the
                 * board. Fall through to the single-line horizontal scroll
                 * below, which truncates instead of showing nothing.
                 */
              }
            }
        }

      {
        int y = text_row(dev, valign) - fontext_ascent();
        int ret = hazk03_display_animate(panel, 0, y, dev->width, line,
                                         false, IPC_TEXT_SCROLL_PERIOD_MS,
                                         IPC_TEXT_SCROLL_STEP, true, false,
                                         0, 0, (const uint8_t *)s, len);

        if (ret == OK)
          {
            return;
          }

        /* The scrolled source did not fit the source buffer of the board.
         * This happens with a long text on the narrow panel. Fall back to
         * the truncated static draw instead of showing nothing.
         */
      }
    }

  draw_text(dev, s, len, align, valign);
}

/* Put a text of the board on one panel. These texts go in the middle. */

static void draw_default(struct sm1626d_dev_s *dev, const char *s)
{
  draw_text_or_scroll(dev, s, strlen(s), HAZK03_ALIGN_CENTRE,
                      HAZK03_VALIGN_MIDDLE);
}

/* Put the content of the board on the panels that the edge MCU has not
 * taken. The sub panel takes a new text when the day changes.
 */

static void display_defaults(const struct tm *t)
{
  if (g_sub_default && t->tm_wday != g_sub_wday)
    {
      g_sub_wday = t->tm_wday;
      draw_default(&g_sub, g_weekday[t->tm_wday % 7]);
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

/* The scan of one row, from the timer.
 *
 * Note: this runs in an interrupt. The work is the transfer of one row, and
 * the wait between the rows costs no CPU. Thus the tasks of the board keep
 * the time that the earlier loop took for its wait.
 *
 * Note: the lock of the framebuffer is a mutex, and an interrupt takes no
 * mutex. A writer of the framebuffer thus stops this timer around its change,
 * and a scan pass never gives a partial image.
 */

static int display_ontimer(int irq, void *context, void *arg)
{
  int on_us;

  if (STM32_TIM_CHECKINT(g_tim, GTIM_SR_CC1IF))
    {
      /* The time with light for this row is over. */

      STM32_TIM_ACKINT(g_tim, GTIM_SR_CC1IF);
      STM32_TIM_DISABLEINT(g_tim, GTIM_DIER_CC1IE);
      sm1626d_output(false);
    }

  if (!STM32_TIM_CHECKINT(g_tim, GTIM_SR_UIF))
    {
      return OK;
    }

  STM32_TIM_ACKINT(g_tim, GTIM_SR_UIF);

  /* Every bit of the row is in the register of both panels now. The latch
   * gives them to the panels, and the light of this row starts.
   */

  sm1626d_latch();

  /* Both panels take the same level, thus one window of light serves both. */

  on_us = sm1626d_ontime(&g_main, ROW_US);
  if (on_us > 0)
    {
      sm1626d_output(true);

      if (on_us < ROW_US)
        {
          STM32_TIM_SETCOMPARE(g_tim, 1, on_us);
          STM32_TIM_ENABLEINT(g_tim, GTIM_DIER_CC1IE);
        }
    }

  if (++g_slot >= SM1626D_ROWS)
    {
      g_slot = 0;
      g_frames++;

      /* A writer may have finished an image. The change takes effect here,
       * between two images, thus no image mixes the old and the new.
       */

      sm1626d_swapnow(&g_main);
      sm1626d_swapnow(&g_sub);
    }

  /* The transfer of that row belongs to a thread, and not to this interrupt.
   * A thread gives way to every interrupt, thus the UART keeps its bytes
   * while a row goes out.
   */

  nxsem_post(&g_rowsem);

  return OK;
}

/* Send the bits of one row while the panel holds the row of the last latch.
 *
 * Note: this thread takes near a quarter of the CPU. It runs above the task
 * of the protocol, because a late row shows as a dark line on the panel.
 */

static int display_shifter(int argc, char *argv[])
{
  for (; ; )
    {
      nxsem_wait_uninterruptible(&g_rowsem);

      sm1626d_shiftcombined(&g_main, &g_sub, g_slot);
    }

  return 0;
}

static int display_scanner(int argc, char *argv[])
{
  unsigned int ticks = TEMP_EVERY_TICKS;
  unsigned int version_left = VERSION_TICKS;
  time_t last = 0;

  for (; ; )
    {
      /* The timer drives the panels. This thread keeps the time and the
       * temperature, thus it waits and it costs almost no CPU.
       */

      nxsig_usleep(TICK_US);

      anim_tick();

        {
          struct tm tm;
          time_t now;

          /* Read the system clock. The DS3231 with the battery keeps that
           * clock. Thus this step uses no bus. Only the temperature uses the
           * bus.
           *
           * Note: the clock of the panel comes from this value and not from a
           * count of the waits. A wait takes at least its time and often
           * more, thus a count of them drifts and the digits then miss a
           * second from time to time.
           */

          now = time(NULL);

          if (now == last)
            {
              continue;
            }

          last = now;
          now += (time_t)g_utcoffset * 60;
          gmtime_r(&now, &tm);

          /* The greeting takes the main panel after the version. */

          if (version_left > 0 && --version_left == 0 && g_main_default)
            {
              draw_default(&g_main, GREETING);
            }

          if (version_left == 0)
            {
              display_defaults(&tm);
            }

          if (g_i2c != NULL && ++ticks >= TEMP_EVERY_TICKS)
            {
              int16_t raw;

              ticks = 0;

              /* The correction goes on the reading. Thus the panels and the
               * state frame give the same value.
               */

              if (ds3231_temperature(g_i2c, &raw) == OK)
                {
                  g_temp = raw + g_tempoffset;
                }
            }

          display_update_sleep(&tm);

          show_time(&tm, g_temp);
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hazk03_display_text(int panel, const char *s, size_t len, uint8_t align,
                        uint8_t valign)
{
  struct sm1626d_dev_s *dev = (panel == HAZK03_PANEL_SUB) ? &g_sub : &g_main;

  /* The edge MCU owns this panel from now on. Thus the content of the board
   * does not return over its text.
   */

  if (panel == HAZK03_PANEL_SUB)
    {
      g_sub_default = false;
    }
  else
    {
      g_main_default = false;
    }

  /* A prior call may have left this panel scrolling. A new text replaces
   * that scroll, whether or not the new text scrolls in turn.
   */

  hazk03_display_animstop(panel);

  /* A second line must not erase the first, thus a text that takes the top or
   * the bottom clears its own rows alone.
   */

  if (valign == HAZK03_VALIGN_MIDDLE)
    {
      nxmutex_lock(&g_fblock);
      sm1626d_begin(dev);
      sm1626d_clear(dev);
      sm1626d_commit(dev);
      nxmutex_unlock(&g_fblock);
    }

  draw_text_or_scroll(dev, s, len, align, valign);

  return OK;
}

int hazk03_display_pixels(int panel, int x, int y, int w, int h,
                          const uint8_t *bits)
{
  struct sm1626d_dev_s *dev = (panel == HAZK03_PANEL_SUB) ? &g_sub : &g_main;

  /* The edge MCU owns this panel from now on. */

  if (panel == HAZK03_PANEL_SUB)
    {
      g_sub_default = false;
    }
  else
    {
      g_main_default = false;
    }

  nxmutex_lock(&g_fblock);
  sm1626d_begin(dev);
  sm1626d_drawbitmap(dev, x, y, w, h, bits);
  sm1626d_commit(dev);
  nxmutex_unlock(&g_fblock);

  return OK;
}

int hazk03_display_animate(int panel, int x, int y, int w, int h,
                           bool vertical, uint16_t period_ms, uint8_t step,
                           bool text, bool file, int srcw, int srch,
                           const uint8_t *src, size_t srclen)
{
  struct display_anim_s *a = &g_anim[(panel == HAZK03_PANEL_SUB) ? 1 : 0];
  uint8_t *buf = (panel == HAZK03_PANEL_SUB) ? g_anim_src_sub
                                             : g_anim_src_main;
  size_t cap = (panel == HAZK03_PANEL_SUB) ? ANIM_SRC_SUB : ANIM_SRC_MAIN;
  size_t need;

  /* A sprite in the flash carries its own step. The caller sends none.
   * It also carries its own frame size. A caller may leave w/h at 0 and
   * take them from the file below, instead of restating them by hand.
   */

  if (period_ms == 0 || (step == 0 && !file) || ((w == 0 || h == 0) && !file))
    {
      return -EINVAL;
    }

  /* The edge MCU owns this panel from now on. */

  if (panel == HAZK03_PANEL_SUB)
    {
      g_sub_default = false;
    }
  else
    {
      g_main_default = false;
    }

  if (file)
    {
      /* The source is a sprite in the flash of the board. Thus the edge MCU
       * names it, and it sends no pixels at all.
       */

      char path[64];
      uint8_t head[IPC_SPRITE_HEADER];
      int fd;
      size_t want;
      int ret = OK;

      if (hazk03_asset_path(path, sizeof(path), HAZK03_ANIM_DIR,
                            (const char *)src, srclen, HAZK03_ANIM_EXT) < 0)
        {
          return -EINVAL;
        }

      fd = open(path, O_RDONLY);
      if (fd < 0)
        {
          return -ENOENT;
        }

      if (read(fd, head, sizeof(head)) != (ssize_t)sizeof(head) ||
          ipc_get_u32(head) != IPC_SPRITE_MAGIC)
        {
          close(fd);
          return -EINVAL;
        }

      srcw = ipc_get_u16(&head[4]);
      srch = ipc_get_u16(&head[6]);
      step = head[8];
      vertical = (head[9] & IPC_ANIM_VERTICAL) != 0;

      want = (size_t)(((srcw + 7) / 8) * srch);

      if (srcw <= 0 || srch <= 0 || step == 0 || want > cap)
        {
          close(fd);
          return -E2BIG;
        }

      /* A window left at 0/0 takes the frame size from the file: the frame
       * width is the file's step, the frame height is the file's height.
       * This is what stops a window wider than one frame from showing part
       * of the next one beside it.
       */

      if (w == 0)
        {
          w = step;
        }

      if (h == 0)
        {
          h = srch;
        }

      nxmutex_lock(&g_fblock);

      if (read(fd, buf, want) != (ssize_t)want)
        {
          ret = -EIO;
        }

      nxmutex_unlock(&g_fblock);
      close(fd);

      if (ret < 0)
        {
          return ret;
        }
    }
  else if (text)
    {
      /* The board draws the text itself. Thus a message that scrolls costs
       * one frame of the protocol, and not one frame for each step.
       *
       * The source wraps at its own end back to its own start. A gap of
       * blank columns after the text keeps the wrap from reading as the
       * text running into itself.
       */

      srcw = sm1626d_textwidth((const char *)src, srclen) +
             (TEXT_SCROLL_GAP_CHARS * fontext_advance());
      srch = h;

      if (srcw < w)
        {
          srcw = w;
        }
    }

  if (srcw <= 0 || srch <= 0)
    {
      return -EINVAL;
    }

  need = (size_t)(((srcw + 7) / 8) * srch);

  if (need > cap)
    {
      return -E2BIG;
    }

  nxmutex_lock(&g_fblock);

  if (text)
    {
      sm1626d_rendertext(buf, srcw, srch, (const char *)src, srclen);
    }
  else if (!file)
    {
      memcpy(buf, src, (srclen < need) ? srclen : need);
    }

  a->vertical   = vertical;
  a->x          = (uint8_t)x;
  a->y          = (uint8_t)y;
  a->w          = (uint8_t)w;
  a->h          = (uint8_t)h;
  a->period_ms  = period_ms;
  a->step       = step;
  a->srcw       = (uint16_t)srcw;
  a->srch       = (uint16_t)srch;
  a->offset     = 0;
  a->elapsed_ms = 0;
  a->src        = buf;
  a->active     = true;

  anim_draw((panel == HAZK03_PANEL_SUB) ? &g_sub : &g_main, a);
  nxmutex_unlock(&g_fblock);

  return OK;
}

void hazk03_display_animstop(int panel)
{
  g_anim[(panel == HAZK03_PANEL_SUB) ? 1 : 0].active = false;
}

int hazk03_display_animspeed(int panel, uint16_t period_ms, uint8_t step)
{
  struct display_anim_s *a = &g_anim[(panel == HAZK03_PANEL_SUB) ? 1 : 0];

  if (!a->active || period_ms == 0)
    {
      return -EINVAL;
    }

  a->period_ms = period_ms;

  if (step > 0)
    {
      a->step = step;
    }

  /* The elapsed time goes back, thus a slower rate takes effect at once and
   * does not wait for the period that was already running.
   */

  a->elapsed_ms = 0;

  return OK;
}

void hazk03_display_clear(int panel)
{
  struct sm1626d_dev_s *dev = (panel == HAZK03_PANEL_SUB) ? &g_sub : &g_main;

  /* An animation would draw over the panel again at its next step. */

  hazk03_display_animstop(panel);

  /* The edge MCU owns this panel from now on, thus the greeting and the day
   * of the week do not return over an empty panel.
   */

  if (panel == HAZK03_PANEL_SUB)
    {
      g_sub_default = false;
    }
  else
    {
      g_main_default = false;
    }

  nxmutex_lock(&g_fblock);
  sm1626d_begin(dev);
  sm1626d_clear(dev);
  sm1626d_commit(dev);
  nxmutex_unlock(&g_fblock);
}

int hazk03_display_brightness(uint8_t digits, uint8_t panels)
{
  g_bright_digits = digits;
  g_bright_panels = panels;

  /* The display stays off during the sleep period. The levels above are in
   * the settings, thus the display takes them at the end of that period.
   */

  if (!g_asleep)
    {
      display_apply_brightness(digits, panels);
    }

  display_persist();

  return OK;
}

void hazk03_display_tempoffset(int16_t tenths)
{
  g_tempoffset = tenths;
  display_persist();
}

void hazk03_display_sleep(uint16_t sleepmin, uint16_t wakemin)
{
  g_sleepmin = sleepmin;
  g_wakemin  = wakemin;

  /* A new period takes effect at the next tick of the scan loop. An end of
   * the period gives the levels of the settings again.
   */

  display_persist();
}

void hazk03_display_utcoffset(int16_t minutes)
{
  g_utcoffset = minutes;
  display_persist();
}

#ifdef CONFIG_MTD_W25
void hazk03_display_setconfig(const struct hazk03_config_s *cfg)
{
  /* These values come from the store, thus this function writes nothing back
   * to it. A write for each of them would also give a record that holds one
   * new value and one default value.
   */

  g_utcoffset     = cfg->utcoffset;
  g_bright_digits = cfg->digits;
  g_bright_panels = cfg->panels;
  g_tempoffset    = cfg->tempoffset;
  g_sleepmin      = cfg->sleepmin;
  g_wakemin       = cfg->wakemin;

  display_apply_brightness(cfg->digits, cfg->panels);
}
#endif

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

  draw_default(&g_main, HAZK03_VERSION);

  /* The timer paces the rows. Its tick is one microsecond, thus the period
   * and the time with light are both in microseconds.
   */

  g_tim = stm32_tim_init(DISPLAY_TIMER);
  if (g_tim == NULL)
    {
      syslog(LOG_ERR, "ERROR: TIM%d not available\n", DISPLAY_TIMER);
      return -ENODEV;
    }

  STM32_TIM_SETCLOCK(g_tim, 1000000);
  STM32_TIM_SETPERIOD(g_tim, ROW_US);
  STM32_TIM_SETISR(g_tim, display_ontimer, NULL, 0);

  /* The update starts a row, and the channel 1 ends the light of that row. */

  STM32_TIM_ENABLEINT(g_tim, GTIM_DIER_UIE);
  /* The transfer of a row holds the CPU longer than one byte of the UART at
   * 460800 baud, which is 21.7 us. Thus this interrupt takes a priority below
   * the others, and the UART takes its bytes while a row goes out.
   */

  STM32_TIM_SETMODE(g_tim, STM32_TIM_MODE_UP);

  ret = kthread_create("shift", SHIFT_PRIORITY, SHIFT_STACKSIZE,
                       display_shifter, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: display shifter: %d\n", ret);
      return ret;
    }

  ret = kthread_create("display", DISPLAY_PRIORITY, DISPLAY_STACKSIZE,
                       display_scanner, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: display scanner: %d\n", ret);
      return ret;
    }

  return OK;
}
