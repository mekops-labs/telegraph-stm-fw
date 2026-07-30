/* SPDX-License-Identifier: Apache-2.0 */

/* The names of the assets in the flash.
 *
 * Note: the board keeps each kind of asset in one place, and it takes one
 * format for each kind. Thus the edge MCU names an asset alone, and this file
 * gives the full path of that name.
 */

#include <nuttx/config.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hazk03.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hazk03_asset_path(char *buf, size_t len, const char *dir, const char *name,
                      size_t namelen, const char *ext) {
    size_t i;
    int n;

    if (namelen == 0 || namelen > HAZK03_ASSET_NAME_MAX) {
        return -EINVAL;
    }

    /* A name with a separator reaches a file outside the place of its kind. */

    for (i = 0; i < namelen; i++) {
        if (name[i] == '/' || name[i] == '\0') {
            return -EINVAL;
        }
    }

    n = snprintf(buf, len, "%s/%.*s%s", dir, (int)namelen, name, ext);

    return (n < 0 || (size_t)n >= len) ? -EINVAL : OK;
}

size_t hazk03_asset_list(char *buf, size_t len, const char *dir,
                         const char *ext) {
    size_t extlen = strlen(ext);
    size_t used = 0;
    struct dirent *e;
    DIR *d;

    d = opendir(dir);
    if (d == NULL) {
        return 0;
    }

    while ((e = readdir(d)) != NULL) {
        size_t namelen = strlen(e->d_name);

        /* The name keeps its ending in the file system, but the caller of the
         * protocol never gives one. Thus the list drops it.
         */

        if (namelen <= extlen ||
            strcmp(&e->d_name[namelen - extlen], ext) != 0) {
            continue;
        }

        namelen -= extlen;

        if (used + namelen + 1 > len) {
            break;
        }

        memcpy(&buf[used], e->d_name, namelen);
        used += namelen;
        buf[used++] = '\n';
    }

    closedir(d);

    return used;
}
