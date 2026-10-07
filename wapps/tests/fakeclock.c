/* SPDX-License-Identifier: Apache-2.0 */

/* A shim for the host test of tg-time, loaded with LD_PRELOAD. It keeps the
 * offset of a clock that the test sets and adds it to CLOCK_REALTIME.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#define NS_PER_S 1000000000LL

static volatile int64_t g_offset_ns;

static int real_gettime(clockid_t id, struct timespec *ts) {
    static int (*fn)(clockid_t, struct timespec *);

    if (fn == NULL) {
        fn = (int (*)(clockid_t, struct timespec *))dlsym(RTLD_NEXT,
                                                          "clock_gettime");
    }

    return fn(id, ts);
}

int clock_gettime(clockid_t id, struct timespec *ts) {
    int rc = real_gettime(id, ts);

    if (rc == 0 && id == CLOCK_REALTIME) {
        int64_t ns = (int64_t)ts->tv_sec * NS_PER_S + ts->tv_nsec + g_offset_ns;

        ts->tv_sec = (time_t)(ns / NS_PER_S);
        ts->tv_nsec = (long)(ns % NS_PER_S);
    }

    return rc;
}

int clock_settime(clockid_t id, const struct timespec *ts) {
    struct timespec now;

    if (id != CLOCK_REALTIME || ts == NULL) {
        errno = EINVAL;
        return -1;
    }

    real_gettime(CLOCK_REALTIME, &now);
    g_offset_ns = ((int64_t)ts->tv_sec - (int64_t)now.tv_sec) * NS_PER_S +
                  ((int64_t)ts->tv_nsec - (int64_t)now.tv_nsec);
    return 0;
}
