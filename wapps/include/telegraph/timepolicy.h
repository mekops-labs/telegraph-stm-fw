/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TELEGRAPH_TIMEPOLICY_H
#define __TELEGRAPH_TIMEPOLICY_H

/* The policy of the wapp tg-time: which clock it reads and which one it
 * writes. The code is pure. The wapp reads the clocks and carries out what
 * this code decides.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Who last set the clock of the engine: the text of its source node. */

enum tg_time_source {
    TG_TIME_SRC_NONE,
    TG_TIME_SRC_RTC,
    TG_TIME_SRC_SNTP,
    TG_TIME_SRC_SERVER,
    TG_TIME_SRC_MANUAL,
    TG_TIME_SRC_UNKNOWN,
};

/* A time of the STM32 below the floor is the default of a DS3231 that lost its
 * battery, and not a time of today. The ceiling is the last second the engine
 * clock holds.
 */

#define TG_TIME_FLOOR_S 1735689600u /* 2025-01-01 00:00:00 UTC */
#define TG_TIME_CEIL_S 4102444799u  /* 2099-12-31 23:59:59 UTC */

/* The two clocks differ by more than this many seconds before the wapp
 * writes one of them.
 */

#define TG_TIME_THRESHOLD_S 2u

/* The largest offset of the local time from UTC, in minutes, either way. */

#define TG_TIME_OFFSET_MAX_MIN 840

struct tg_time_in {
    enum tg_time_source source; /* the source node of the engine             */
    bool engine_valid;          /* its status reads valid, and time was read */
    uint32_t engine_s;
    bool stm_known; /* the STM32 answered the request of the state */
    uint32_t stm_s;
    bool offset_due; /* the STM32 reset, or this wapp sent no offset yet */
};

enum tg_time_kind {
    TG_TIME_NONE,         /* nothing to do                                  */
    TG_TIME_WRITE_ENGINE, /* write the time to the engine, source rtc       */
    TG_TIME_PUSH_STM,     /* send the time of the engine, and the offset    */
};

struct tg_time_action {
    enum tg_time_kind kind;
    uint32_t seconds;
};

/* A time the wapp trusts from the STM32. */

bool tg_time_plausible(uint32_t seconds);

/* Decide the next step.
 *
 * The engine has no time of its own, or an unset clock: take the time of the
 * STM32, if it is plausible. The engine has a time from the control plane or a
 * person: send it to the STM32 when the two differ by more than the
 * threshold, or the STM32 holds no plausible time, or the offset is due. The
 * wapp never writes the engine clock back in that case. The source rtc came
 * from the STM32 itself, thus the wapp only sends the offset.
 */

struct tg_time_action tg_time_decide(const struct tg_time_in *in);

/* The text of a source node, with or without a final newline. */

enum tg_time_source tg_time_parse_source(const char *text, size_t len);

/* The offset from the config mount: a line `offset_min=<minutes>`. Returns 0
 * and the value when the line is correct or absent, where absent gives 0.
 * Returns -1 and 0 for a value that is not a number or above the limit.
 */

int tg_time_parse_offset(const char *conf, size_t len, int *offset_min);

/* True when the count of the accepted frames of the STM32 fell below the last
 * one, which only a reset of the STM32 does. A first reading counts as a
 * reset, thus the offset goes out at the start.
 */

bool tg_time_stm_reset(bool have_last, uint16_t last, uint16_t now);

#endif /* __TELEGRAPH_TIMEPOLICY_H */
