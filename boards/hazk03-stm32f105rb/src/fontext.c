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

static uint16_t g_cp[FONTEXT_MAX_GLYPHS];
static uint16_t g_cols[FONTEXT_MAX_GLYPHS][FONTEXT_WIDTH];

static struct fontext_s g_big = {
    .cp = g_cp,
    .cols = &g_cols[0][0],
    .count = 0,
    .width = FONTEXT_WIDTH,
    .rows = FONTEXT_ROWS,
    .ascent = FONTEXT_ASCENT,
    .stride = FONTEXT_WIDTH,
    .ascii_fallback = true,
};

static uint16_t fontext_get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

const struct fontext_s *fontext_big(void) { return &g_big; }

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

/* Read every entry without storing it: the code points must ascend, and the
 * file must hold all of them.
 */

static int fontext_check(int fd, const char *path, const uint8_t *header,
                         uint16_t count) {
    uint8_t entry[FONTEXT_ENTRY_LEN];
    size_t entrylen = 2 + ((size_t)header[6] * 2);
    int prev = -1;
    uint16_t i;

    for (i = 0; i < count; i++) {
        int cp;

        if (read(fd, entry, entrylen) != (ssize_t)entrylen) {
            syslog(LOG_ERR, "font: %s ends in entry %u\n", path, i);
            return -EINVAL;
        }

        cp = fontext_get_u16(&entry[0]);

        if (cp <= prev) {
            syslog(LOG_ERR, "font: %s is not in code point order\n", path);
            return -EINVAL;
        }

        prev = cp;
    }

    return OK;
}

int fontext_load(const char *path) {
    uint8_t header[FONTEXT_HEADER_LEN];
    uint8_t entry[FONTEXT_ENTRY_LEN];
    size_t entrylen;
    uint16_t count;
    uint16_t i;
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

    ret = fontext_check(fd, path, header, count);
    if (ret < 0) {
        goto done;
    }

    /* The file is good. A read error from here on empties the table, since the
     * entries before it are already replaced.
     */

    entrylen = 2 + ((size_t)header[6] * 2);

    if (lseek(fd, FONTEXT_HEADER_LEN, SEEK_SET) != FONTEXT_HEADER_LEN) {
        ret = -EIO;
        goto done;
    }

    for (i = 0; i < count; i++) {
        int col;

        if (read(fd, entry, entrylen) != (ssize_t)entrylen) {
            g_big.count = 0;
            ret = -EIO;
            goto done;
        }

        g_cp[i] = fontext_get_u16(&entry[0]);

        for (col = 0; col < FONTEXT_WIDTH; col++) {
            g_cols[i][col] =
                (col < header[6]) ? fontext_get_u16(&entry[2 + (col * 2)]) : 0;
        }
    }

    g_big.count = count;
    g_big.width = header[6];
    g_big.rows = header[7];
    g_big.ascent = header[8];
    ret = OK;

    syslog(LOG_INFO, "font: %s gives %u characters in a cell of %d by %d\n",
           path, count, g_big.width, g_big.rows);

done:
    close(fd);
    return ret;
}
