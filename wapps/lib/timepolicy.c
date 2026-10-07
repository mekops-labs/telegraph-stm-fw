/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <telegraph/timepolicy.h>

#define OFFSET_KEY "offset_min="
#define OFFSET_DIGITS_MAX 5u

static const struct {
    const char *text;
    enum tg_time_source source;
} g_sources[] = {
    {"none", TG_TIME_SRC_NONE},     {"rtc", TG_TIME_SRC_RTC},
    {"sntp", TG_TIME_SRC_SNTP},     {"server", TG_TIME_SRC_SERVER},
    {"manual", TG_TIME_SRC_MANUAL},
};

bool tg_time_plausible(uint32_t seconds) {
    return seconds >= TG_TIME_FLOOR_S && seconds <= TG_TIME_CEIL_S;
}

static uint32_t distance(uint32_t a, uint32_t b) {
    return a > b ? a - b : b - a;
}

struct tg_time_action tg_time_decide(const struct tg_time_in *in) {
    struct tg_time_action act = {TG_TIME_NONE, 0};

    /* The engine holds no time that something vouched for: the STM32, which a
     * battery keeps, is the only source.
     */

    if (in->source == TG_TIME_SRC_NONE || in->source == TG_TIME_SRC_UNKNOWN ||
        !in->engine_valid) {
        if (in->stm_known && tg_time_plausible(in->stm_s)) {
            act.kind = TG_TIME_WRITE_ENGINE;
            act.seconds = in->stm_s;
        }

        return act;
    }

    act.seconds = in->engine_s;

    /* The time of the engine came from the STM32, thus there is no difference
     * to correct. Only the offset can be due.
     */

    if (in->source == TG_TIME_SRC_RTC) {
        act.kind = in->offset_due ? TG_TIME_PUSH_STM : TG_TIME_NONE;
        return act;
    }

    if (in->offset_due || !in->stm_known || !tg_time_plausible(in->stm_s) ||
        distance(in->engine_s, in->stm_s) > TG_TIME_THRESHOLD_S) {
        act.kind = TG_TIME_PUSH_STM;
    }

    return act;
}

enum tg_time_source tg_time_parse_source(const char *text, size_t len) {
    if (text == NULL) {
        return TG_TIME_SRC_UNKNOWN;
    }

    if (len > 0 && text[len - 1] == '\n') {
        len--;
    }

    for (size_t i = 0; i < sizeof(g_sources) / sizeof(g_sources[0]); i++) {
        if (strlen(g_sources[i].text) == len &&
            memcmp(g_sources[i].text, text, len) == 0) {
            return g_sources[i].source;
        }
    }

    return TG_TIME_SRC_UNKNOWN;
}

/* The value that starts at `at` and ends at the first newline or at `end`. */

static int parse_value(const char *at, const char *end, int *value) {
    bool negative = false;
    unsigned int digits = 0;
    long v = 0;

    if (at < end && *at == '-') {
        negative = true;
        at++;
    }

    while (at < end && *at != '\n') {
        if (*at < '0' || *at > '9' || digits >= OFFSET_DIGITS_MAX) {
            return -1;
        }

        v = v * 10 + (*at - '0');
        digits++;
        at++;
    }

    if (digits == 0) {
        return -1;
    }

    if (negative) {
        v = -v;
    }

    if (v > TG_TIME_OFFSET_MAX_MIN || v < -TG_TIME_OFFSET_MAX_MIN) {
        return -1;
    }

    *value = (int)v;
    return 0;
}

int tg_time_parse_offset(const char *conf, size_t len, int *offset_min) {
    const size_t keylen = sizeof(OFFSET_KEY) - 1;
    const char *end = conf + len;

    *offset_min = 0;
    if (conf == NULL) {
        return 0;
    }

    /* A key counts at the start of a line alone. */

    for (const char *line = conf; line < end;) {
        if ((size_t)(end - line) >= keylen &&
            memcmp(line, OFFSET_KEY, keylen) == 0) {
            int v = 0;

            if (parse_value(line + keylen, end, &v) < 0) {
                return -1;
            }

            *offset_min = v;
            return 0;
        }

        const char *nl = memchr(line, '\n', (size_t)(end - line));

        if (nl == NULL) {
            break;
        }

        line = nl + 1;
    }

    return 0;
}

bool tg_time_stm_reset(bool have_last, uint16_t last, uint16_t now) {
    return !have_last || now < last;
}
