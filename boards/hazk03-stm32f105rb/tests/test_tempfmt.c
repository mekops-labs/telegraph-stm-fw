/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <unity.h>

#include "../src/tempfmt.h"

void setUp(void) {}

void tearDown(void) {}

static void expect_frames(int degrees, int count, const char *first,
                          const char *second) {
    char frames[TEMPFMT_MAX_FRAMES][TEMPFMT_DIGITS];

    memset(frames, '?', sizeof(frames));

    TEST_ASSERT_EQUAL_INT(count, tempfmt_frames(degrees, frames));
    TEST_ASSERT_EQUAL_CHAR_ARRAY(first, frames[0], TEMPFMT_DIGITS);

    if (count > 1) {
        TEST_ASSERT_EQUAL_CHAR_ARRAY(second, frames[1], TEMPFMT_DIGITS);
    }
}

static void test_degrees_round_half_away_from_zero(void) {
    TEST_ASSERT_EQUAL_INT(24, tempfmt_degrees(237));
    TEST_ASSERT_EQUAL_INT(23, tempfmt_degrees(234));
    TEST_ASSERT_EQUAL_INT(24, tempfmt_degrees(235));
    TEST_ASSERT_EQUAL_INT(-24, tempfmt_degrees(-235));
    TEST_ASSERT_EQUAL_INT(-23, tempfmt_degrees(-234));
    TEST_ASSERT_EQUAL_INT(0, tempfmt_degrees(4));
    TEST_ASSERT_EQUAL_INT(0, tempfmt_degrees(-4));
}

static void test_degrees_clamp_to_the_range(void) {
    TEST_ASSERT_EQUAL_INT(TEMPFMT_MAX, tempfmt_degrees(30000));
    TEST_ASSERT_EQUAL_INT(TEMPFMT_MIN, tempfmt_degrees(-30000));
}

static void test_one_digit_is_right_aligned(void) {
    expect_frames(5, 1, " 5", NULL);
    expect_frames(0, 1, " 0", NULL);
}

static void test_two_digits_fill_the_frame(void) {
    expect_frames(24, 1, "24", NULL);
    expect_frames(99, 1, "99", NULL);
}

static void test_a_negative_digit_shows_its_sign(void) {
    expect_frames(-5, 1, "-5", NULL);
}

static void test_three_characters_alternate(void) {
    expect_frames(-12, 2, " -", "12");
    expect_frames(-99, 2, " -", "99");
    expect_frames(100, 2, " 1", "00");
    expect_frames(199, 2, " 1", "99");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_degrees_round_half_away_from_zero);
    RUN_TEST(test_degrees_clamp_to_the_range);
    RUN_TEST(test_one_digit_is_right_aligned);
    RUN_TEST(test_two_digits_fill_the_frame);
    RUN_TEST(test_a_negative_digit_shows_its_sign);
    RUN_TEST(test_three_characters_alternate);

    return UNITY_END();
}
