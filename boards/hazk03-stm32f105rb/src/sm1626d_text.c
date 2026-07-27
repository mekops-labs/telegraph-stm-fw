/* SPDX-License-Identifier: Apache-2.0 */

/* The text renderer of the dot-matrix panels.
 *
 * Note: this file is separate from the driver. Thus a build without text
 * pays no flash for the font.
 */

#include <nuttx/config.h>

#include "font5x7.h"
#include "sm1626d.h"

void sm1626d_drawtext(struct sm1626d_dev_s *dev, int x, int y,
                      const char *s, size_t len)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      const uint8_t *glyph = font5x7_glyph(s[i]);
      int col;

      for (col = 0; col < FONT5X7_WIDTH; col++)
        {
          int row;

          for (row = 0; row < FONT5X7_HEIGHT; row++)
            {
              if (glyph[col] & (1u << row))
                {
                  sm1626d_drawpixel(dev, x + col, y + row, true);
                }
            }
        }

      x += FONT5X7_ADVANCE;

      if (x >= dev->width)
        {
          break;
        }
    }
}
