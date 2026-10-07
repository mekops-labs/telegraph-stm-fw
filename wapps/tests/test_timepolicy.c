/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <unity.h>

#include <telegraph/timepolicy.h>

#define NOW 1791300000u

void setUp(void) {}
void tearDown(void) {}

static struct tg_time_in input(enum tg_time_source source, uint32_t engine,
                               uint32_t stm) {
    struct tg_time_in in = {
        source, source != TG_TIME_SRC_NONE, engine, true, stm, false};

    return in;
}

/* tg_time_plausible */

void test_plausible_floor_and_ceiling(void) {
    TEST_ASSERT_FALSE(tg_time_plausible(0));
    TEST_ASSERT_FALSE(tg_time_plausible(946684800u)); /* 2000-01-01 */
    TEST_ASSERT_FALSE(tg_time_plausible(TG_TIME_FLOOR_S - 1u));
    TEST_ASSERT_TRUE(tg_time_plausible(TG_TIME_FLOOR_S));
    TEST_ASSERT_TRUE(tg_time_plausible(NOW));
    TEST_ASSERT_TRUE(tg_time_plausible(TG_TIME_CEIL_S));
    TEST_ASSERT_FALSE(tg_time_plausible(TG_TIME_CEIL_S + 1u));
    TEST_ASSERT_FALSE(tg_time_plausible(0xffffffffu));
}

/* The engine has no time: the STM32 is the source */

void test_none_takes_the_time_of_the_stm32(void) {
    struct tg_time_in in = input(TG_TIME_SRC_NONE, 0, NOW);
    struct tg_time_action a = tg_time_decide(&in);

    TEST_ASSERT_EQUAL(TG_TIME_WRITE_ENGINE, a.kind);
    TEST_ASSERT_EQUAL_UINT32(NOW, a.seconds);
}

void test_none_ignores_an_stm32_time_below_the_floor(void) {
    struct tg_time_in in = input(TG_TIME_SRC_NONE, 0, 946684800u);

    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
    in.stm_s = TG_TIME_FLOOR_S - 1u;
    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
    in.stm_s = TG_TIME_FLOOR_S;
    TEST_ASSERT_EQUAL(TG_TIME_WRITE_ENGINE, tg_time_decide(&in).kind);
}

void test_none_ignores_an_stm32_time_above_the_ceiling(void) {
    struct tg_time_in in = input(TG_TIME_SRC_NONE, 0, TG_TIME_CEIL_S + 1u);

    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
}

void test_none_without_an_answer_does_nothing(void) {
    struct tg_time_in in = input(TG_TIME_SRC_NONE, 0, NOW);

    in.stm_known = false;
    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
}

void test_none_does_not_push_the_offset(void) {
    struct tg_time_in in = input(TG_TIME_SRC_NONE, 0, 0);

    in.offset_due = true;
    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
}

void test_an_engine_that_reads_invalid_counts_as_having_no_time(void) {
    struct tg_time_in in = input(TG_TIME_SRC_MANUAL, NOW + 99, NOW);

    in.engine_valid = false;
    TEST_ASSERT_EQUAL(TG_TIME_WRITE_ENGINE, tg_time_decide(&in).kind);
}

void test_an_unknown_source_counts_as_having_no_time(void) {
    struct tg_time_in in = input(TG_TIME_SRC_UNKNOWN, NOW + 99, NOW);

    TEST_ASSERT_EQUAL(TG_TIME_WRITE_ENGINE, tg_time_decide(&in).kind);
}

/* The engine has a time of the control plane or a person: push, never pull */

void test_sntp_within_the_threshold_leaves_the_stm32(void) {
    struct tg_time_in in = input(TG_TIME_SRC_SNTP, NOW, NOW);

    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
    in.stm_s = NOW - TG_TIME_THRESHOLD_S;
    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
    in.stm_s = NOW + TG_TIME_THRESHOLD_S;
    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
}

void test_sntp_beyond_the_threshold_pushes_the_engine_time(void) {
    struct tg_time_in in = input(TG_TIME_SRC_SNTP, NOW, NOW - 3u);
    struct tg_time_action a = tg_time_decide(&in);

    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, a.kind);
    TEST_ASSERT_EQUAL_UINT32(NOW, a.seconds);

    in.stm_s = NOW + 3u;
    a = tg_time_decide(&in);
    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, a.kind);
    TEST_ASSERT_EQUAL_UINT32(NOW, a.seconds);
}

void test_each_authoritative_source_pushes(void) {
    enum tg_time_source sources[] = {TG_TIME_SRC_SNTP, TG_TIME_SRC_SERVER,
                                     TG_TIME_SRC_MANUAL};

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        struct tg_time_in in = input(sources[i], NOW, NOW - 60u);

        TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, tg_time_decide(&in).kind);
    }
}

void test_a_difference_in_the_other_direction_still_pushes(void) {
    struct tg_time_in in = input(TG_TIME_SRC_MANUAL, NOW, NOW + 3600u);

    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, tg_time_decide(&in).kind);
    TEST_ASSERT_EQUAL_UINT32(NOW, tg_time_decide(&in).seconds);
}

void test_an_invalid_stm32_time_gets_the_engine_time(void) {
    struct tg_time_in in = input(TG_TIME_SRC_SNTP, NOW, 946684800u);

    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, tg_time_decide(&in).kind);
    in.stm_s = 0;
    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, tg_time_decide(&in).kind);
}

void test_a_silent_stm32_gets_the_engine_time(void) {
    struct tg_time_in in = input(TG_TIME_SRC_SNTP, NOW, NOW);

    in.stm_known = false;
    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, tg_time_decide(&in).kind);
}

void test_the_offset_due_pushes_even_when_the_clocks_agree(void) {
    struct tg_time_in in = input(TG_TIME_SRC_SNTP, NOW, NOW);

    in.offset_due = true;
    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, tg_time_decide(&in).kind);
}

/* The source rtc came from the STM32 */

void test_rtc_never_pulls_and_never_pushes_a_difference(void) {
    struct tg_time_in in = input(TG_TIME_SRC_RTC, NOW, NOW + 600u);

    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
    in.stm_known = false;
    TEST_ASSERT_EQUAL(TG_TIME_NONE, tg_time_decide(&in).kind);
}

void test_rtc_sends_the_offset_when_it_is_due(void) {
    struct tg_time_in in = input(TG_TIME_SRC_RTC, NOW, NOW);
    struct tg_time_action a;

    in.offset_due = true;
    a = tg_time_decide(&in);
    TEST_ASSERT_EQUAL(TG_TIME_PUSH_STM, a.kind);
    TEST_ASSERT_EQUAL_UINT32(NOW, a.seconds);
}

/* The source node */

void test_parse_source_names(void) {
    TEST_ASSERT_EQUAL(TG_TIME_SRC_NONE, tg_time_parse_source("none\n", 5));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_RTC, tg_time_parse_source("rtc\n", 4));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_SNTP, tg_time_parse_source("sntp\n", 5));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_SERVER, tg_time_parse_source("server\n", 7));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_MANUAL, tg_time_parse_source("manual\n", 7));
}

void test_parse_source_accepts_no_newline(void) {
    TEST_ASSERT_EQUAL(TG_TIME_SRC_SNTP, tg_time_parse_source("sntp", 4));
}

void test_parse_source_refuses_everything_else(void) {
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source("", 0));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source("\n", 1));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source("sntpx\n", 6));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source("sn\n", 3));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source("SNTP\n", 5));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source("rtc \n", 5));
    TEST_ASSERT_EQUAL(TG_TIME_SRC_UNKNOWN, tg_time_parse_source(NULL, 4));
}

void test_parse_source_reads_the_length_not_a_terminator(void) {
    TEST_ASSERT_EQUAL(TG_TIME_SRC_RTC, tg_time_parse_source("rtcx", 3));
}

/* The offset */

void test_offset_absent_is_zero(void) {
    int off = 99;

    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset("", 0, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
    off = 99;
    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset("other=1\n", 8, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
    off = 99;
    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset(NULL, 0, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
}

void test_offset_reads_a_signed_value(void) {
    int off = 0;

    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset("offset_min=60\n", 14, &off));
    TEST_ASSERT_EQUAL_INT(60, off);
    TEST_ASSERT_EQUAL_INT(0,
                          tg_time_parse_offset("offset_min=-330\n", 16, &off));
    TEST_ASSERT_EQUAL_INT(-330, off);
    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset("offset_min=0", 12, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
}

void test_offset_finds_its_line_among_others(void) {
    const char *conf = "a=1\noffset_min=120\nb=2\n";
    int off = 0;

    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset(conf, strlen(conf), &off));
    TEST_ASSERT_EQUAL_INT(120, off);
}

void test_offset_limits(void) {
    int off = 7;

    TEST_ASSERT_EQUAL_INT(0,
                          tg_time_parse_offset("offset_min=840\n", 15, &off));
    TEST_ASSERT_EQUAL_INT(840, off);
    TEST_ASSERT_EQUAL_INT(0,
                          tg_time_parse_offset("offset_min=-840\n", 16, &off));
    TEST_ASSERT_EQUAL_INT(-840, off);
    TEST_ASSERT_EQUAL_INT(-1,
                          tg_time_parse_offset("offset_min=841\n", 15, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
    off = 7;
    TEST_ASSERT_EQUAL_INT(-1,
                          tg_time_parse_offset("offset_min=-841\n", 16, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
}

void test_offset_refuses_text_that_is_no_number(void) {
    int off = 7;

    TEST_ASSERT_EQUAL_INT(-1, tg_time_parse_offset("offset_min=\n", 12, &off));
    TEST_ASSERT_EQUAL_INT(0, off);
    TEST_ASSERT_EQUAL_INT(-1, tg_time_parse_offset("offset_min=x\n", 13, &off));
    TEST_ASSERT_EQUAL_INT(-1,
                          tg_time_parse_offset("offset_min=1x\n", 14, &off));
    TEST_ASSERT_EQUAL_INT(-1,
                          tg_time_parse_offset("offset_min=+60\n", 15, &off));
    TEST_ASSERT_EQUAL_INT(-1, tg_time_parse_offset("offset_min=-\n", 13, &off));
    TEST_ASSERT_EQUAL_INT(-1,
                          tg_time_parse_offset("offset_min=1 \n", 14, &off));
    TEST_ASSERT_EQUAL_INT(
        -1, tg_time_parse_offset("offset_min=99999999999\n", 23, &off));
}

void test_offset_value_ends_at_the_length(void) {
    int off = 0;

    TEST_ASSERT_EQUAL_INT(0, tg_time_parse_offset("offset_min=601", 13, &off));
    TEST_ASSERT_EQUAL_INT(60, off);
}

/* The reset of the STM32 */

void test_the_first_reading_counts_as_a_reset(void) {
    TEST_ASSERT_TRUE(tg_time_stm_reset(false, 0, 100));
}

void test_a_counter_that_falls_is_a_reset(void) {
    TEST_ASSERT_TRUE(tg_time_stm_reset(true, 500, 3));
    TEST_ASSERT_TRUE(tg_time_stm_reset(true, 500, 499));
}

void test_a_counter_that_rises_or_holds_is_no_reset(void) {
    TEST_ASSERT_FALSE(tg_time_stm_reset(true, 500, 500));
    TEST_ASSERT_FALSE(tg_time_stm_reset(true, 500, 501));
    TEST_ASSERT_FALSE(tg_time_stm_reset(true, 0, 65535));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plausible_floor_and_ceiling);
    RUN_TEST(test_none_takes_the_time_of_the_stm32);
    RUN_TEST(test_none_ignores_an_stm32_time_below_the_floor);
    RUN_TEST(test_none_ignores_an_stm32_time_above_the_ceiling);
    RUN_TEST(test_none_without_an_answer_does_nothing);
    RUN_TEST(test_none_does_not_push_the_offset);
    RUN_TEST(test_an_engine_that_reads_invalid_counts_as_having_no_time);
    RUN_TEST(test_an_unknown_source_counts_as_having_no_time);
    RUN_TEST(test_sntp_within_the_threshold_leaves_the_stm32);
    RUN_TEST(test_sntp_beyond_the_threshold_pushes_the_engine_time);
    RUN_TEST(test_each_authoritative_source_pushes);
    RUN_TEST(test_a_difference_in_the_other_direction_still_pushes);
    RUN_TEST(test_an_invalid_stm32_time_gets_the_engine_time);
    RUN_TEST(test_a_silent_stm32_gets_the_engine_time);
    RUN_TEST(test_the_offset_due_pushes_even_when_the_clocks_agree);
    RUN_TEST(test_rtc_never_pulls_and_never_pushes_a_difference);
    RUN_TEST(test_rtc_sends_the_offset_when_it_is_due);
    RUN_TEST(test_parse_source_names);
    RUN_TEST(test_parse_source_accepts_no_newline);
    RUN_TEST(test_parse_source_refuses_everything_else);
    RUN_TEST(test_parse_source_reads_the_length_not_a_terminator);
    RUN_TEST(test_offset_absent_is_zero);
    RUN_TEST(test_offset_reads_a_signed_value);
    RUN_TEST(test_offset_finds_its_line_among_others);
    RUN_TEST(test_offset_limits);
    RUN_TEST(test_offset_refuses_text_that_is_no_number);
    RUN_TEST(test_offset_value_ends_at_the_length);
    RUN_TEST(test_the_first_reading_counts_as_a_reset);
    RUN_TEST(test_a_counter_that_falls_is_a_reset);
    RUN_TEST(test_a_counter_that_rises_or_holds_is_no_reset);
    return UNITY_END();
}
