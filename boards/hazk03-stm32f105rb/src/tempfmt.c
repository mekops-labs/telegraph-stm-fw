/* SPDX-License-Identifier: Apache-2.0 */

#include "tempfmt.h"

#define TEMPFMT_BASE 10
#define TEMPFMT_HALF 5
#define TEMPFMT_TEXT_MAX 4

int tempfmt_degrees(int16_t tenths) {
    int value = tenths;
    int degrees;

    degrees = (value >= 0 ? value + TEMPFMT_HALF : value - TEMPFMT_HALF) /
              TEMPFMT_BASE;

    if (degrees < TEMPFMT_MIN) {
        degrees = TEMPFMT_MIN;
    } else if (degrees > TEMPFMT_MAX) {
        degrees = TEMPFMT_MAX;
    }

    return degrees;
}

int tempfmt_frames(int degrees,
                   char frames[TEMPFMT_MAX_FRAMES][TEMPFMT_DIGITS]) {
    char text[TEMPFMT_TEXT_MAX];
    int len = 0;
    int magnitude = degrees < 0 ? -degrees : degrees;

    if (degrees < 0) {
        text[len++] = '-';
    }

    if (magnitude >= TEMPFMT_BASE * TEMPFMT_BASE) {
        text[len++] = (char)('0' + (magnitude / (TEMPFMT_BASE * TEMPFMT_BASE)));
    }

    if (magnitude >= TEMPFMT_BASE) {
        text[len++] = (char)('0' + ((magnitude / TEMPFMT_BASE) % TEMPFMT_BASE));
    }

    text[len++] = (char)('0' + (magnitude % TEMPFMT_BASE));

    if (len <= TEMPFMT_DIGITS) {
        frames[0][0] = len == 2 ? text[0] : ' ';
        frames[0][1] = text[len - 1];

        return 1;
    }

    frames[0][0] = ' ';
    frames[0][1] = text[0];
    frames[1][0] = text[1];
    frames[1][1] = text[2];

    return 2;
}
