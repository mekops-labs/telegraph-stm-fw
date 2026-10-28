/* SPDX-License-Identifier: Apache-2.0 */

/* The big font, read from a file in the flash. The font of the firmware holds
 * the ASCII table alone; this file adds the letters of the other alphabets.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "fontext.h"

#define FONTEXT_MAX_ROWS 16

/* Two tables: a load fills the one not in use and then publishes it, thus a
 * drawing call that took the font keeps a consistent table throughout.
 */

#define FONTEXT_BANKS 2

static uint16_t g_cp[FONTEXT_BANKS][FONTEXT_MAX_GLYPHS];
static uint16_t g_cols[FONTEXT_BANKS][FONTEXT_MAX_GLYPHS][FONTEXT_WIDTH];

static struct fontext_s g_banks[FONTEXT_BANKS] = {
    {
        .cp = g_cp[0],
        .cols = &g_cols[0][0][0],
        .count = 0,
        .width = FONTEXT_WIDTH,
        .rows = FONTEXT_ROWS,
        .ascent = FONTEXT_ASCENT,
        .stride = FONTEXT_WIDTH,
        .ascii_fallback = true,
    },
    {
        .cp = g_cp[1],
        .cols = &g_cols[1][0][0],
        .count = 0,
        .width = FONTEXT_WIDTH,
        .rows = FONTEXT_ROWS,
        .ascent = FONTEXT_ASCENT,
        .stride = FONTEXT_WIDTH,
        .ascii_fallback = true,
    },
};

/* One word, written after the table is complete. */

static struct fontext_s *volatile g_big = &g_banks[0];

static uint16_t fontext_get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

const struct fontext_s *fontext_big(void) { return g_big; }

/* Read the header and check it. Returns the entry count, or a negative errno.
 */

static int fontext_header(int fd, const char *path, uint8_t *header) {
    uint16_t count;

    if (read(fd, header, FONTEXT_HEADER_LEN) != (ssize_t)FONTEXT_HEADER_LEN) {
        return -EINVAL;
    }

    if (fontext_get_u16(&header[0]) != (uint16_t)(FONTEXT_MAGIC & 0xffff) ||
        fontext_get_u16(&header[2]) != (uint16_t)(FONTEXT_MAGIC >> 16)) {
        syslog(LOG_ERR, "font: %s is not a font\n", path);
        return -EINVAL;
    }

    count = fontext_get_u16(&header[4]);

    /* The file carries its own cell. A cell larger than the store of this build
     * is the only one that it refuses.
     */

    if (header[6] == 0 || header[6] > FONTEXT_WIDTH || header[7] == 0 ||
        header[7] > FONTEXT_MAX_ROWS || header[8] >= header[7]) {
        syslog(LOG_ERR, "font: %s has a cell of %u by %u that does not fit\n",
               path, header[6], header[7]);
        return -EINVAL;
    }

    if (count > FONTEXT_MAX_GLYPHS) {
        syslog(LOG_ERR, "font: %s holds %u characters, the limit is %u\n", path,
               count, FONTEXT_MAX_GLYPHS);
        return -EINVAL;
    }

    return count;
}

int fontext_load(const char *path) {
    uint8_t header[FONTEXT_HEADER_LEN];
    uint8_t entry[FONTEXT_ENTRY_LEN];
    int bank = (g_big == &g_banks[0]) ? 1 : 0;
    struct fontext_s *next = &g_banks[bank];
    size_t entrylen;
    uint16_t count;
    uint16_t i;
    int prev = -1;
    int fd;
    int ret;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -ENOENT;
    }

    ret = fontext_header(fd, path, header);
    if (ret < 0) {
        goto done;
    }

    count = (uint16_t)ret;
    entrylen = 2 + ((size_t)header[6] * 2);

    for (i = 0; i < count; i++) {
        int col;
        int cp;

        if (read(fd, entry, entrylen) != (ssize_t)entrylen) {
            syslog(LOG_ERR, "font: %s ends in entry %u\n", path, i);
            ret = -EINVAL;
            goto done;
        }

        cp = fontext_get_u16(&entry[0]);

        if (cp <= prev) {
            syslog(LOG_ERR, "font: %s is not in code point order\n", path);
            ret = -EINVAL;
            goto done;
        }

        prev = cp;
        g_cp[bank][i] = (uint16_t)cp;

        for (col = 0; col < FONTEXT_WIDTH; col++) {
            g_cols[bank][i][col] =
                (col < header[6]) ? fontext_get_u16(&entry[2 + (col * 2)]) : 0;
        }
    }

    next->count = count;
    next->width = header[6];
    next->rows = header[7];
    next->ascent = header[8];
    g_big = next;
    ret = OK;

    syslog(LOG_INFO, "font: %s gives %u characters in a cell of %d by %d\n",
           path, count, next->width, next->rows);

done:
    close(fd);
    return ret;
}
