/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TELEGRAPH_HASSPARSE_H
#define TELEGRAPH_HASSPARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parse a decimal degree such as "-12.5" into tenths. False for anything
 * else, and for a value outside IPC_EXTTEMP_MIN to IPC_EXTTEMP_MAX.
 */

bool hass_parse_tenths(const char *text, int *tenths);

/* The length of the longest prefix of at most max bytes that ends on a UTF-8
 * character boundary.
 */

size_t hass_utf8_prefix(const char *text, size_t len, size_t max);

/* Replace each two-character "\n" with a line feed, in place. Returns the new
 * length.
 */

size_t hass_unescape_newlines(char *text, size_t len);

#endif /* TELEGRAPH_HASSPARSE_H */
