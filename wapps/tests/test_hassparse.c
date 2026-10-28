/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <unity.h>

#include <telegraph/hassparse.h>

void setUp(void) {}

void tearDown(void) {}

static void expect_tenths(const char *text, int want) {
    int got = 12345;

    TEST_ASSERT_TRUE_MESSAGE(hass_parse_tenths(text, &got), text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, got, text);
}

static void expect_rejected(const char *text) {
    int got = 12345;

    TEST_ASSERT_FALSE_MESSAGE(hass_parse_tenths(text, &got), text);
    TEST_ASSERT_EQUAL_INT(12345, got);
}

static void test_parse_accepts_decimals(void) {
    expect_tenths("0", 0);
    expect_tenths("5", 50);
    expect_tenths("21.5", 215);
    expect_tenths("-12", -120);
    expect_tenths("-0.5", -5);
    expect_tenths("+3.0", 30);
    expect_tenths("199.9", 1999);
    expect_tenths("-99", -990);
}

static void test_parse_rounds_the_second_digit(void) {
    expect_tenths("21.54", 215);
    expect_tenths("21.55", 216);
    expect_tenths("-21.55", -216);
    expect_tenths("21.5999", 216);
}

static void test_parse_rejects_text_and_malformed_numbers(void) {
    expect_rejected("");
    expect_rejected("-");
    expect_rejected("abc");
    expect_rejected("12abc");
    expect_rejected("1.");
    expect_rejected(".5");
    expect_rejected("1.2.3");
    expect_rejected("12 ");
    expect_rejected("--1");
    expect_rejected("1e2");
}

static void test_parse_rejects_values_out_of_range(void) {
    expect_rejected("200");
    expect_rejected("199.95");
    expect_rejected("-99.1");
    expect_rejected("-100");
    expect_rejected("99999");
}

static void test_utf8_prefix_keeps_short_text(void) {
    TEST_ASSERT_EQUAL_UINT(3, hass_utf8_prefix("abc", 3, 64));
    TEST_ASSERT_EQUAL_UINT(0, hass_utf8_prefix("", 0, 64));
}

static void test_utf8_prefix_cuts_on_a_boundary(void) {
    /* "zażółć" is 6 characters in 10 bytes; a 2-byte letter starts at 2. */
    const char *text = "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87";

    TEST_ASSERT_EQUAL_UINT(8, hass_utf8_prefix(text, 10, 8));
    TEST_ASSERT_EQUAL_UINT(8, hass_utf8_prefix(text, 10, 9));
    TEST_ASSERT_EQUAL_UINT(2, hass_utf8_prefix(text, 10, 3));
    TEST_ASSERT_EQUAL_UINT(4, hass_utf8_prefix(text, 10, 5));
    TEST_ASSERT_EQUAL_UINT(10, hass_utf8_prefix(text, 10, 10));
}

static void test_unescape_replaces_the_two_characters(void) {
    char text[16];
    size_t len;

    memcpy(text, "Dom\\nOK", 7);
    len = hass_unescape_newlines(text, 7);

    TEST_ASSERT_EQUAL_UINT(6, len);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("Dom\nOK", text, 6);
}

static void test_unescape_keeps_a_real_line_feed_and_other_escapes(void) {
    char text[16];
    size_t len;

    memcpy(text, "a\nb\\tc\\", 7);
    len = hass_unescape_newlines(text, 7);

    TEST_ASSERT_EQUAL_UINT(7, len);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("a\nb\\tc\\", text, 7);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_accepts_decimals);
    RUN_TEST(test_parse_rounds_the_second_digit);
    RUN_TEST(test_parse_rejects_text_and_malformed_numbers);
    RUN_TEST(test_parse_rejects_values_out_of_range);
    RUN_TEST(test_utf8_prefix_keeps_short_text);
    RUN_TEST(test_utf8_prefix_cuts_on_a_boundary);
    RUN_TEST(test_unescape_replaces_the_two_characters);
    RUN_TEST(test_unescape_keeps_a_real_line_feed_and_other_escapes);

    return UNITY_END();
}
