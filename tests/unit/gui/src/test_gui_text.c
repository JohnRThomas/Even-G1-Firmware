/*
 * Text drawing with the built-in fonts. x1 - x0 limits the line width, y1 - y0 only where a line
 * may start; font 0 lines are 27 rows apart.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_gui.h"
#include "gui_test.h"

static void gui_before(void *fixture)
{
	ARG_UNUSED(fixture);

	gui_test_fb_reset();
	clear_showing_notification_on_gui();
	text_grey_level = 0x55;
	gui_test_seed_cjk_font();
}

ZTEST_SUITE(gui, NULL, NULL, gui_before, NULL, NULL);

static int draw(const char *s, int x0, int y0, int x1, int y1, uint32_t max_lines,
		uint32_t dark_cnt)
{
	return gui_utf_draw(0, s, 0, x0, y0, x1, y1, max_lines, dark_cnt, 0, NULL, 0);
}

ZTEST(gui, test_utf_draw_ascii)
{
	zassert_equal(draw("Hello, G1!", 40, 20, 300, 60, 1, 0), 0, NULL);
	gui_expect_golden("utf_draw_ascii", GUI_PBM, 32, 16, 276, 48);
}

ZTEST(gui, test_utf_draw_chinese)
{
	zassert_equal(draw("中日田口", 40, 20, 300, 60, 1, 0), 0, NULL);
	gui_expect_golden("utf_draw_chinese", GUI_PBM, 32, 16, 276, 48);
}

ZTEST(gui, test_utf_draw_mixed)
{
	zassert_equal(draw("Even 中日 G1", 40, 20, 300, 60, 1, 0), 0, NULL);
	gui_expect_golden("utf_draw_mixed", GUI_PBM, 32, 16, 276, 48);
}

ZTEST(gui, test_utf_draw_wraps_at_box_width_and_stops_at_max_lines)
{
	draw("The quick brown fox jumps over the lazy dog", 40, 20, 200, 160, 2, 0);
	gui_expect_golden("utf_draw_wrap", GUI_PBM, 32, 16, 184, 112);
}

/* CR and LF each end a line */
ZTEST(gui, test_utf_draw_newlines)
{
	draw("Line one\nLine two\r\nThird", 40, 20, 300, 180, 5, 0);
	gui_expect_golden("utf_draw_newlines", GUI_PBM, 32, 16, 276, 140);
}

/* 0x55 = level 5 */
ZTEST(gui, test_utf_draw_dims_first_dark_cnt_characters)
{
	draw("Hello", 40, 20, 300, 60, 1, 2);
	gui_expect_golden("utf_draw_dark_cnt", GUI_PGM, 32, 16, 120, 40);
}

ZTEST(gui, test_utf_draw_family_empty_string_draws_nothing)
{
	zassert_equal(draw("", 40, 20, 300, 60, 1, 0), 0, NULL);
	zassert_equal(gui_utf_Wordwrap_draw(0, "", 0, 40, 20, 300, 60, 1, 0, 0, NULL, 0), 0, NULL);
	zassert_equal(gui_utf_draw_truncate(0, "", 0, 40, 20, 300, 60, 1, 0, 0, NULL, 0), 0, NULL);
	zassert_equal(gui_utf_draw_middle(0, "", 0, 40, 20, 300, 60, 1, 0, 0, NULL, 0), 0, NULL);
	zassert_equal(gui_utf_draw_align_right(0, "", 0, 40, 20, 300, 60, 1, 0, 0, NULL, 0), 0,
		      NULL);
	zassert_equal(gui_utf_draw_dark_light_split(0, "", 0, 40, 20, 300, 60, 1, '*', 0, NULL, 0),
		      0, NULL);
	zassert_equal(gui_utf_draw_darkword_by_lines(0, "", 0, 40, 20, 300, 60, 1, 1, 0, NULL, 0),
		      0, NULL);

	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
	zassert_true(gui_test_guard_intact(), NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.call_count, 0, NULL);
}

/* 二 is not in the stand-in font */
ZTEST(gui, test_utf_draw_skips_glyphs_missing_from_font)
{
	draw("二二", 40, 20, 300, 60, 1, 0);
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

ZTEST(gui, test_utf_draw_while_notification_showing_clears_box_and_refreshes_panel)
{
	for (int y = 20; y < 60; y++) {
		for (int x = 40; x < 300; x++) {
			gui_test_px_set(x, y, 0x3);
		}
	}
	g1_state()->panel.current_row = 7;
	g1_state()->panel.current_column = 9;
	set_showing_notification_on_gui();

	draw("Hello, G1!", 40, 20, 300, 60, 1, 0);
	clear_showing_notification_on_gui();

	zassert_equal(_reflash_fb_data_to_lcd_fake.call_count, 1, NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.arg0_val, 7, NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.arg1_val, 9, NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.arg2_val, 40, NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.arg3_val, 20, NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.arg4_val, 300, NULL);
	zassert_equal(_reflash_fb_data_to_lcd_fake.arg5_val, 60, NULL);
	/* same pixels: the grey fill is gone */
	gui_expect_golden("utf_draw_ascii", GUI_PBM, 32, 16, 276, 48);
}

static int effect_cb_calls;
static const char *effect_cb_str;
static int effect_cb(int arg, const char *str, int font, int x, int y)
{
	effect_cb_calls++;
	effect_cb_str = str;
	zassert_equal(arg, 0, NULL);
	zassert_equal(font, 0, NULL);
	zassert_equal(x, 40, NULL);
	zassert_equal(y, 20, NULL);
	return 7;
}

ZTEST(gui, test_utf_draw_effect_callback_replaces_drawing)
{
	static const char s[] = "Hello";

	effect_cb_calls = 0;
	zassert_equal(gui_utf_draw(0, s, 0, 40, 20, 300, 60, 1, 0, 0, effect_cb, 0), 7, NULL);
	zassert_equal(effect_cb_calls, 1, NULL);
	zassert_equal(effect_cb_str, s, NULL);
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

/* words break once the line is past half the box */
ZTEST(gui, test_wordwrap_draw_keeps_words_whole)
{
	gui_utf_Wordwrap_draw(0, "The quick brown fox jumps over the lazy dog", 0, 40, 20, 200, 160,
			      4, 0, 0, NULL, 0);
	gui_expect_golden("wordwrap", GUI_PBM, 32, 16, 184, 140);
}

ZTEST(gui, test_truncate_draw_ends_last_line_with_ellipsis)
{
	gui_utf_draw_truncate(0, "The quick brown fox jumps over the lazy dog", 0, 40, 20, 200, 80,
			      2, 0, 0, NULL, 0);
	gui_expect_golden("truncate", GUI_PBM, 32, 16, 192, 84);
}

ZTEST(gui, test_truncate_draw_short_text_has_no_ellipsis)
{
	static uint8_t plain[GUI_FB_ROWS * GUI_FB_ROW_BYTES];

	draw("Hello, G1!", 40, 20, 300, 60, 1, 0);
	gui_test_fb_save(plain);
	gui_test_fb_reset();

	gui_utf_draw_truncate(0, "Hello, G1!", 0, 40, 20, 300, 60, 1, 0, 0, NULL, 0);
	zassert_true(gui_test_fb_equal(plain), NULL);
}

ZTEST(gui, test_middle_draw_centres_each_line)
{
	gui_utf_draw_middle(0, "Centered\nsecond line", 0, 40, 20, 360, 100, 3, 0, 0, NULL, 0);
	gui_expect_golden("middle", GUI_PBM, 32, 16, 336, 88);
}

/* start rounded down to even x */
ZTEST(gui, test_align_right_draw_aligns_each_line_to_x1)
{
	gui_utf_draw_align_right(0, "Right\naligned text", 0, 40, 20, 360, 100, 3, 0, 0, NULL, 0);
	gui_expect_golden("align_right", GUI_PBM, 32, 16, 336, 88);
}

ZTEST(gui, test_dark_light_split_dims_between_split_codes)
{
	gui_utf_draw_dark_light_split(0, "Tap *here* now", 0, 40, 20, 300, 60, 1, '*', 0, NULL, 0);
	gui_expect_golden("dark_light_split", GUI_PGM, 32, 16, 176, 40);
}

ZTEST(gui, test_dark_light_split_state_does_not_leak_into_next_call)
{
	static uint8_t plain[GUI_FB_ROWS * GUI_FB_ROW_BYTES];

	gui_utf_draw_dark_light_split(0, "a*b", 0, 40, 100, 300, 140, 1, '*', 0, NULL, 0);
	gui_test_fb_reset();
	draw("Hello", 40, 20, 300, 60, 1, 0);
	gui_test_fb_save(plain);
	gui_test_fb_reset();

	gui_utf_draw_dark_light_split(0, "Hello", 0, 40, 20, 300, 60, 1, '*', 0, NULL, 0);
	zassert_true(gui_test_fb_equal(plain), NULL);
}

ZTEST(gui, test_darkword_by_lines_dims_first_lines)
{
	gui_utf_draw_darkword_by_lines(0, "first line\nsecond line\nthird line", 0, 40, 20, 300, 120,
				       4, 1, 0, NULL, 0);
	gui_expect_golden("darkword_by_lines", GUI_PGM, 32, 16, 176, 100);
}

/* Defect: a glyph is dimmed before it wraps, so line 2 starts dim. */
ZTEST_EXPECT_FAIL(gui, test_darkword_by_lines_wrapped_line_starts_bright);
ZTEST(gui, test_darkword_by_lines_wrapped_line_starts_bright)
{
	int max = 0;

	gui_utf_draw_darkword_by_lines(0, "mmmmmmmmmmmmmmmmmmmm", 0, 40, 20, 200, 120, 4, 1, 0, NULL,
				       0);
	zassert_true(gui_test_lit(40, 47, 200, 73) > 0, "no second line");
	for (int y = 47; y < 73; y++) {
		for (int x = 40; x < 50; x++) {
			max = MAX(max, gui_test_px(x, y));
		}
	}
	zassert_equal(max, 15, "first glyph of line 2 drawn at level %d", max);
}

/* Origin <= 32: drawn one line below, scrolled up in 9 steps. */
ZTEST(gui, test_scroll_line_low_origin_replaces_line)
{
	static const int steps[9] = {3, 6, 9, 12, 15, 18, 21, 24, 27};

	draw("old text", 40, 40, 300, 67, 1, 0);
	gui_utf_draw_scroll_line(0, "new text", 0, 40, 40, 300, 67, 1, 0);
	zassert_true(gui_flags_get() & GUI_FLAG_NOTIFICATION_SHOWING,
		     "scroll_line leaves the notification flag set");
	clear_showing_notification_on_gui();

	zassert_equal(_reflash_fb_data_to_lcd_ex_fake.call_count, 9, NULL);
	for (int i = 0; i < 9; i++) {
		zassert_equal(_reflash_fb_data_to_lcd_ex_fake.arg6_history[i], steps[i], "step %d", i);
	}
	gui_expect_golden("scroll_line_low", GUI_PBM, 32, 8, 276, 100);
}

/* Origin > 32: the old line moves up, the new one scrolls in from above. */
ZTEST(gui, test_scroll_line_high_origin_replaces_line)
{
	static const int steps[9] = {-27, -21, -18, -15, -12, -9, -6, -3, 0};

	g1_state()->canvas_x = 32;
	g1_state()->canvas_y = 40;
	draw("old text", 40, 60, 300, 87, 1, 0);
	set_showing_notification_on_gui(); /* as left by an earlier scroll_line */
	gui_utf_draw_scroll_line(0, "new text", 0, 40, 60, 300, 87, 1, 0);
	clear_showing_notification_on_gui();

	zassert_equal(_reflash_fb_data_to_lcd_ex_fake.call_count, 9, NULL);
	for (int i = 0; i < 9; i++) {
		zassert_equal(_reflash_fb_data_to_lcd_ex_fake.arg6_history[i], steps[i], "step %d", i);
	}
	gui_expect_golden("scroll_line_high", GUI_PBM, 32, 24, 276, 100);
}
