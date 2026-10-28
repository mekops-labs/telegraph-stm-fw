/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <unity.h>

#include "../src/font5x7.h"
#include "../src/fontext.h"

void setUp(void) {}

void tearDown(void) {}

/* A big font with no entries: every ASCII character comes from the firmware. */

static const struct fontext_s g_empty_big = {
    .cp = NULL,
    .cols = NULL,
    .count = 0,
    .width = FONTEXT_WIDTH,
    .rows = FONTEXT_ROWS,
    .ascent = FONTEXT_ASCENT,
    .stride = FONTEXT_WIDTH,
    .ascii_fallback = true,
};

static bool all_zero(const uint16_t *cols) {
    int col;

    for (col = 0; col < FONTEXT_WIDTH; col++) {
        if (cols[col] != 0) {
            return false;
        }
    }

    return true;
}

static void test_compact_cell(void) {
    const struct fontext_s *font = fontext_compact();

    TEST_ASSERT_EQUAL_INT(3, fontext_width(font));
    TEST_ASSERT_EQUAL_INT(7, fontext_rows(font));
    TEST_ASSERT_EQUAL_INT(7, fontext_lineheight(font));
    TEST_ASSERT_EQUAL_INT(4, fontext_advance(font));
}

static void test_compact_two_lines_fit_the_panel(void) {
    TEST_ASSERT_EQUAL_INT(14, 2 * fontext_lineheight(fontext_compact()));
}

static void test_compact_table_is_sorted_and_fits(void) {
    const struct fontext_s *font = fontext_compact();
    uint16_t i;

    TEST_ASSERT_TRUE(font->count > 0);
    TEST_ASSERT_TRUE(font->count <= FONTEXT_MAX_GLYPHS);

    for (i = 1; i < font->count; i++) {
        TEST_ASSERT_TRUE(font->cp[i - 1] < font->cp[i]);
    }
}

static void test_compact_lowercase_takes_the_capital_shape(void) {
    uint16_t upper[FONTEXT_WIDTH];
    uint16_t lower[FONTEXT_WIDTH];
    const struct fontext_s *font = fontext_compact();

    TEST_ASSERT_EQUAL_UINT(1, fontext_next(font, "A", 1, upper));
    TEST_ASSERT_EQUAL_UINT(1, fontext_next(font, "a", 1, lower));
    TEST_ASSERT_FALSE(all_zero(upper));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(upper, lower, FONTEXT_WIDTH);
}

static void test_compact_has_the_polish_letters(void) {
    static const char *const letters[] = {"ą", "ć", "ę", "ł", "ń", "ó",
                                          "ś", "ź", "ż", "Ą", "Ł", "Ż"};
    uint16_t cols[FONTEXT_WIDTH];
    size_t i;

    for (i = 0; i < sizeof(letters) / sizeof(letters[0]); i++) {
        TEST_ASSERT_EQUAL_UINT(
            2, fontext_next(fontext_compact(), letters[i], 2, cols));
        TEST_ASSERT_FALSE_MESSAGE(all_zero(cols), letters[i]);
    }
}

static void test_compact_missing_characters_are_blank(void) {
    uint16_t cols[FONTEXT_WIDTH];
    const struct fontext_s *font = fontext_compact();

    /* The 5x7 shape would overlap the other line, so there is no fallback. */

    TEST_ASSERT_EQUAL_UINT(1, fontext_next(font, "@", 1, cols));
    TEST_ASSERT_TRUE(all_zero(cols));
    TEST_ASSERT_EQUAL_UINT(1, fontext_next(font, "\n", 1, cols));
    TEST_ASSERT_TRUE(all_zero(cols));
}

static void test_big_falls_back_to_the_firmware_font(void) {
    uint16_t cols[FONTEXT_WIDTH];
    const uint8_t *glyph = font5x7_glyph('A');
    int col;

    TEST_ASSERT_EQUAL_UINT(1, fontext_next(&g_empty_big, "A", 1, cols));

    for (col = 0; col < FONT5X7_WIDTH; col++) {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(glyph[col] << FONTEXT_ASCENT),
                                 cols[col]);
    }
}

static void test_big_line_feed_and_unknown_letters_are_blank(void) {
    uint16_t cols[FONTEXT_WIDTH];

    TEST_ASSERT_EQUAL_UINT(1, fontext_next(&g_empty_big, "\n", 1, cols));
    TEST_ASSERT_TRUE(all_zero(cols));
    TEST_ASSERT_EQUAL_UINT(2, fontext_next(&g_empty_big, "ż", 2, cols));
    TEST_ASSERT_TRUE(all_zero(cols));
}

static void test_each_font_keeps_its_own_cell(void) {
    TEST_ASSERT_EQUAL_INT(2, fontext_ascent(&g_empty_big));
    TEST_ASSERT_EQUAL_INT(1, fontext_ascent(fontext_compact()));
    TEST_ASSERT_EQUAL_INT(10, fontext_lineheight(&g_empty_big));
}

static void test_utf8_lengths(void) {
    uint16_t cols[FONTEXT_WIDTH];

    TEST_ASSERT_EQUAL_UINT(3,
                           fontext_next(&g_empty_big, "\xe2\x82\xac", 3, cols));
    TEST_ASSERT_EQUAL_UINT(1, fontext_next(&g_empty_big, "\x80", 1, cols));
    TEST_ASSERT_EQUAL_UINT(1, fontext_next(&g_empty_big, "\xc5", 1, cols));
    TEST_ASSERT_TRUE(all_zero(cols));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_compact_cell);
    RUN_TEST(test_compact_two_lines_fit_the_panel);
    RUN_TEST(test_compact_table_is_sorted_and_fits);
    RUN_TEST(test_compact_lowercase_takes_the_capital_shape);
    RUN_TEST(test_compact_has_the_polish_letters);
    RUN_TEST(test_compact_missing_characters_are_blank);
    RUN_TEST(test_big_falls_back_to_the_firmware_font);
    RUN_TEST(test_big_line_feed_and_unknown_letters_are_blank);
    RUN_TEST(test_each_font_keeps_its_own_cell);
    RUN_TEST(test_utf8_lengths);

    return UNITY_END();
}
