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

#include <string.h>

#include "font5x7.h"
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

  return (count * fontext_advance()) - 1;
}

void sm1626d_drawtext(struct sm1626d_dev_s *dev, int x, int y,
                      const char *s, size_t len)
{
  size_t i = 0;

  /* The cell is taller than the letter. Thus the top of the cell goes above
   * the top of the letter, and a mark such as an acute has its own rows.
   */

  y -= fontext_ascent();

  while (i < len)
    {
      const uint16_t *cols;
      int col;

      i += fontext_next(&s[i], len - i, &cols);

      for (col = 0; col < fontext_width(); col++)
        {
          int row;

          for (row = 0; row < fontext_rows(); row++)
            {
              if (cols[col] & (1u << row))
                {
                  sm1626d_drawpixel(dev, x + col, y + row, true);
                }
            }
        }

      x += fontext_advance();

      if (x >= dev->width)
        {
          break;
        }
    }
}

/* Draw one text into bits, at the cell whose top is ytop and whose height is
 * cellheight. Both sm1626d_rendertext() and sm1626d_rendertextlines() place
 * one text this way; the second calls this once per wrapped line.
 */

static void rendertext_at(uint8_t *bits, int w, int totalh, int stride,
                          int xoff, int ytop, int cellheight, const char *s,
                          size_t len)
{
  size_t i = 0;
  int x = xoff;

  while (i < len && x < w)
    {
      const uint16_t *cols;
      int col;

      i += fontext_next(&s[i], len - i, &cols);

      for (col = 0; col < fontext_width(); col++)
        {
          int row;

          if (x + col >= w)
            {
              break;
            }

          for (row = 0; row < fontext_rows(); row++)
            {
              /* The cell is taller than the letter. The row 0 of the cell
               * belongs above the letter, thus it moves down here.
               */

              int y = ytop + row - fontext_ascent() +
                      ((cellheight - (fontext_rows() - fontext_ascent() - 1)) / 2);

              if (y < 0 || y >= totalh)
                {
                  continue;
                }

              if (cols[col] & (1u << row))
                {
                  bits[(y * stride) + ((x + col) / 8)] |=
                      (0x80 >> ((x + col) % 8));
                }
            }
        }

      x += fontext_advance();
    }
}

void sm1626d_rendertext(uint8_t *bits, int w, int h, const char *s,
                        size_t len)
{
  int stride = (w + 7) / 8;

  memset(bits, 0, (size_t)(stride * h));
  rendertext_at(bits, w, h, stride, 0, 0, h, s, len);
}

void sm1626d_rendertextlines(uint8_t *bits, int w, int totalh,
                             const size_t *starts, const size_t *lens,
                             const int *xoffs, int n, const char *s)
{
  int stride = (w + 7) / 8;
  int lineheight = fontext_lineheight();
  int k;

  memset(bits, 0, (size_t)(stride * totalh));

  for (k = 0; k < n; k++)
    {
      rendertext_at(bits, w, totalh, stride, xoffs[k], k * lineheight,
                    lineheight, &s[starts[k]], lens[k]);
    }
}
