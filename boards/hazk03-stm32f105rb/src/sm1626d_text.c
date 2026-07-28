/* SPDX-License-Identifier: Apache-2.0 */

/* The text renderer of the dot-matrix panels.
 *
 * Note: this file is separate from the driver. Thus a build without text
 * pays no flash for the font.
 *
 * Note: the text is in UTF-8. The extended font gives the letters outside the
 * ASCII table, and the font of the firmware gives the others.
 */

#include <nuttx/config.h>

#include "fontext.h"
#include "sm1626d.h"

int sm1626d_textwidth(const char *s, size_t len)
{
  const uint16_t *cols;
  size_t i = 0;
  int count = 0;

  while (i < len)
    {
      i += fontext_next(&s[i], len - i, &cols);
      count++;
    }

  if (count == 0)
    {
      return 0;
    }

  /* Each character takes its columns, and a gap follows each one except the
   * last.
   */

  return (count * FONTEXT_ADVANCE) - (FONTEXT_ADVANCE - FONTEXT_WIDTH);
}

void sm1626d_drawtext(struct sm1626d_dev_s *dev, int x, int y,
                      const char *s, size_t len)
{
  size_t i = 0;

  /* The cell is taller than the letter. Thus the top of the cell goes above
   * the top of the letter, and a mark such as an acute has its own rows.
   */

  y -= FONTEXT_ASCENT;

  while (i < len)
    {
      const uint16_t *cols;
      int col;

      i += fontext_next(&s[i], len - i, &cols);

      for (col = 0; col < FONTEXT_WIDTH; col++)
        {
          int row;

          for (row = 0; row < FONTEXT_ROWS; row++)
            {
              if (cols[col] & (1u << row))
                {
                  sm1626d_drawpixel(dev, x + col, y + row, true);
                }
            }
        }

      x += FONTEXT_ADVANCE;

      if (x >= dev->width)
        {
          break;
        }
    }
}
