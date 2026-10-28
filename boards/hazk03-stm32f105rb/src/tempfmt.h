/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEMPFMT_H
#define TEMPFMT_H

#include <stdint.h>

/* The temperature digits hold this many characters. */

#define TEMPFMT_DIGITS 2

/* A value of up to three characters needs two frames. */

#define TEMPFMT_MAX_FRAMES 2

/* The shown range, in whole degrees Celsius. */

#define TEMPFMT_MIN (-99)
#define TEMPFMT_MAX 199

/* Tenths of a degree to whole degrees, half away from zero, clamped to the
 * shown range.
 */

int tempfmt_degrees(int16_t tenths);

/* Split whole degrees into frames of TEMPFMT_DIGITS characters, each frame
 * right-aligned. A value of three characters gives its first character, then
 * its last two. Returns the frame count.
 */

int tempfmt_frames(int degrees,
                   char frames[TEMPFMT_MAX_FRAMES][TEMPFMT_DIGITS]);

#endif /* TEMPFMT_H */
