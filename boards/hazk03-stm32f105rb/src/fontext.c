/* SPDX-License-Identifier: Apache-2.0 */

/* The extended font, and the decoder of UTF-8.
 *
 * Note: the font of the firmware holds the ASCII table alone. This file adds
 * the letters of the other alphabets from a file in the flash.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "font5x7.h"
#include "fontext.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The entries of the font, sorted by their code point. */

static uint16_t g_cp[FONTEXT_MAX_GLYPHS];
static uint16_t g_cols[FONTEXT_MAX_GLYPHS][FONTEXT_WIDTH];
static uint16_t g_count;

/* The columns of one character of the font of the firmware, after the move to
 * the taller cell.
 */

static uint16_t g_fallback[FONTEXT_WIDTH];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint16_t fontext_get_u16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

/* Take one code point from a text in UTF-8.
 *
 * Note: the function takes the sequences of one, two and three bytes. A byte
 * that starts no valid sequence gives the code point of the space.
 */

static size_t fontext_utf8(const char *s, size_t len, uint32_t *cp)
{
  const uint8_t *p = (const uint8_t *)s;

  if ((p[0] & 0x80) == 0)
    {
      *cp = p[0];
      return 1;
    }

  if ((p[0] & 0xe0) == 0xc0 && len >= 2 && (p[1] & 0xc0) == 0x80)
    {
      *cp = (uint32_t)((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
      return 2;
    }

  if ((p[0] & 0xf0) == 0xe0 && len >= 3 && (p[1] & 0xc0) == 0x80 &&
      (p[2] & 0xc0) == 0x80)
    {
      *cp = (uint32_t)((p[0] & 0x0f) << 12) |
            (uint32_t)((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
      return 3;
    }

  *cp = ' ';
  return 1;
}

/* Look for one code point in the extended font. The entries go up, thus the
 * search divides the range at each step.
 */

static const uint16_t *fontext_find(uint32_t cp)
{
  int lo = 0;
  int hi = (int)g_count - 1;

  if (cp > 0xffff)
    {
      return NULL;
    }

  while (lo <= hi)
    {
      int mid = (lo + hi) / 2;

      if (g_cp[mid] == (uint16_t)cp)
        {
          return g_cols[mid];
        }

      if (g_cp[mid] < (uint16_t)cp)
        {
          lo = mid + 1;
        }
      else
        {
          hi = mid - 1;
        }
    }

  return NULL;
}

/* Move one character of the font of the firmware into the taller cell. */

static const uint16_t *fontext_fromascii(uint32_t cp)
{
  const uint8_t *glyph = font5x7_glyph((char)cp);
  int col;

  for (col = 0; col < FONTEXT_WIDTH; col++)
    {
      g_fallback[col] = (uint16_t)(glyph[col] << FONTEXT_ASCENT);
    }

  return g_fallback;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int fontext_load(const char *path)
{
  uint8_t header[FONTEXT_HEADER_LEN];
  uint8_t entry[FONTEXT_ENTRY_LEN];
  uint16_t count;
  uint16_t i;
  int fd;
  int ret = -EINVAL;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -ENOENT;
    }

  if (read(fd, header, sizeof(header)) != (ssize_t)sizeof(header))
    {
      goto done;
    }

  if (fontext_get_u16(&header[0]) != (uint16_t)(FONTEXT_MAGIC & 0xffff) ||
      fontext_get_u16(&header[2]) != (uint16_t)(FONTEXT_MAGIC >> 16))
    {
      syslog(LOG_ERR, "font: %s is not a font\n", path);
      goto done;
    }

  count = fontext_get_u16(&header[4]);

  if (header[6] != FONTEXT_WIDTH || header[7] != FONTEXT_ROWS ||
      header[8] != FONTEXT_ASCENT)
    {
      syslog(LOG_ERR, "font: %s has another cell\n", path);
      goto done;
    }

  if (count > FONTEXT_MAX_GLYPHS)
    {
      syslog(LOG_ERR, "font: %s holds %u characters, the limit is %u\n",
             path, count, FONTEXT_MAX_GLYPHS);
      goto done;
    }

  for (i = 0; i < count; i++)
    {
      int col;

      if (read(fd, entry, sizeof(entry)) != (ssize_t)sizeof(entry))
        {
          goto done;
        }

      g_cp[i] = fontext_get_u16(&entry[0]);

      for (col = 0; col < FONTEXT_WIDTH; col++)
        {
          g_cols[i][col] = fontext_get_u16(&entry[2 + (col * 2)]);
        }
    }

  g_count = count;
  ret = OK;

  syslog(LOG_INFO, "font: %s gives %u characters\n", path, count);

done:
  close(fd);
  return ret;
}

size_t fontext_next(const char *s, size_t len, const uint16_t **cols)
{
  const uint16_t *found;
  uint32_t cp;
  size_t used;

  used = fontext_utf8(s, len, &cp);

  found = fontext_find(cp);
  if (found != NULL)
    {
      *cols = found;
      return used;
    }

  /* The font of the firmware holds the ASCII table. Any other character has
   * no shape, thus it gives a space.
   */

  *cols = fontext_fromascii((cp >= 0x20 && cp <= 0x7e) ? cp : ' ');

  return used;
}
