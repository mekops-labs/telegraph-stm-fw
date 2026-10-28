/* SPDX-License-Identifier: Apache-2.0 */

#include <telegraph/hassparse.h>
#include <telegraph/ipc.h>

#define DECIMAL 10
#define ROUND_AT 5
#define UTF8_CONT_MASK 0xc0
#define UTF8_CONT 0x80

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool hass_parse_tenths(const char *text, int *tenths) {
    bool negative = false;
    int whole = 0;
    int frac = 0;
    int next = 0;
    int value;

    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        text++;
    }

    if (!is_digit(*text)) {
        return false;
    }

    while (is_digit(*text)) {
        whole = (whole * DECIMAL) + (*text - '0');
        if (whole > IPC_EXTTEMP_MAX) {
            return false;
        }
        text++;
    }

    if (*text == '.') {
        text++;

        if (!is_digit(*text)) {
            return false;
        }

        frac = *text++ - '0';

        if (is_digit(*text)) {
            next = *text++ - '0';
        }

        while (is_digit(*text)) {
            text++;
        }
    }

    if (*text != '\0') {
        return false;
    }

    value = (whole * DECIMAL) + frac + (next >= ROUND_AT ? 1 : 0);
    value = negative ? -value : value;

    if (value < IPC_EXTTEMP_MIN || value > IPC_EXTTEMP_MAX) {
        return false;
    }

    *tenths = value;

    return true;
}

size_t hass_utf8_prefix(const char *text, size_t len, size_t max) {
    size_t cut = len < max ? len : max;

    while (cut > 0 && cut < len &&
           ((unsigned char)text[cut] & UTF8_CONT_MASK) == UTF8_CONT) {
        cut--;
    }

    return cut;
}

size_t hass_unescape_newlines(char *text, size_t len) {
    size_t in = 0;
    size_t out = 0;

    while (in < len) {
        if (text[in] == '\\' && in + 1 < len && text[in + 1] == 'n') {
            text[out++] = '\n';
            in += 2;
        } else {
            text[out++] = text[in++];
        }
    }

    return out;
}
