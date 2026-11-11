/* Clock, bars, 1-bpp bitmaps, fade-out, kerning and the text origin. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_gui.h"
#include "gui_test.h"

/* not declared in gui.h */
int gui_font_kerning(uint16_t cur, uint16_t next);

static uint8_t saved[GUI_FB_ROWS * GUI_FB_ROW_BYTES];

/* 2024-01-01 00:00 UTC plus the time of day */
#define TS_0005 (1704067200u + 5 * 60)
#define TS_0907 (1704067200u + 9 * 3600 + 7 * 60)
#define TS_1230 (1704067200u + 12 * 3600 + 30 * 60)
#define TS_1345 (1704067200u + 13 * 3600 + 45 * 60)

static void expect_clock_text(uint32_t ts, bool is_24h, int mode, int font, const char *text)
{
	gui_utf_draw(0, text, font, 40, 20, 300, 60, 1, 0, 0, NULL, 0);
	gui_test_fb_save(saved);
	gui_test_fb_reset();

	zassert_equal(gui_clock_draw(ts, 40, 20, 300, 60, mode, is_24h), 0, NULL);
	zassert_true(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS) > 0, "nothing drawn");
	zassert_true(gui_test_fb_equal(saved), "clock does not read \"%s\"", text);
}

ZTEST(gui, test_clock_24h_two_digit_hour_and_minute)
{
	expect_clock_text(TS_0907, true, 0, 0, "09:07");
}

ZTEST(gui, test_clock_24h_afternoon)
{
	expect_clock_text(TS_1345, true, 0, 0, "13:45");
}

ZTEST(gui, test_clock_12h_afternoon_subtracts_12)
{
	expect_clock_text(TS_1345, false, 0, 0, "01:45");
}

ZTEST(gui, test_clock_12h_midnight_hour_is_12)
{
	expect_clock_text(TS_0005, false, 0, 0, "12:05");
}

ZTEST(gui, test_clock_12h_noon_hour_is_12)
{
	expect_clock_text(TS_1230, false, 0, 0, "12:30");
}

ZTEST(gui, test_clock_mode_3_draws_text_in_font_3)
{
	expect_clock_text(TS_1345, true, 3, 3, "13:45");
}

ZTEST(gui, test_clock_mode_1_24h)
{
	gui_clock_draw(TS_1345, 40, 20, 300, 60, 1, true);
	gui_expect_golden("clock_mode1_24h", GUI_PBM, 32, 16, 276, 44);
}

ZTEST(gui, test_clock_mode_2_12h)
{
	gui_clock_draw(TS_1345, 40, 20, 300, 70, 2, false);
	gui_expect_golden("clock_mode2_12h", GUI_PBM, 32, 16, 276, 52);
}

/* thumb top at y0 + percent * (y1 - y0 - 13) / 100 */
ZTEST(gui, test_vertical_bar_thumb_position_follows_percent)
{
	zassert_equal(gui_verticalLine_process_bar(100, 20, 100, 180, 0, 0, 0), 0, NULL);
	zassert_equal(gui_verticalLine_process_bar(140, 20, 140, 180, 0, 50, 0), 0, NULL);
	zassert_equal(gui_verticalLine_process_bar(180, 20, 180, 180, 0, 100, 0), 0, NULL);
	gui_expect_golden("vertical_bars", GUI_PGM, 88, 16, 112, 168);
}

ZTEST(gui, test_vertical_bar_percent_clamped_to_100)
{
	gui_verticalLine_process_bar(180, 20, 180, 180, 0, 100, 0);
	gui_test_fb_save(saved);
	gui_test_fb_reset();

	gui_verticalLine_process_bar(180, 20, 180, 180, 0, 250, 0);
	zassert_true(gui_test_fb_equal(saved), NULL);
}

ZTEST(gui, test_vertical_bar_rejects_bad_geometry)
{
	zassert_equal(gui_verticalLine_process_bar(100, 20, 102, 180, 0, 50, 0), -1, NULL);
	zassert_equal(gui_verticalLine_process_bar(100, 180, 100, 20, 0, 50, 0), -1, NULL);
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

ZTEST(gui, test_horizontal_bar_segments_and_fill)
{
	zassert_equal(gui_horizontal_percentage_bar(40, 20, 360, 28, 1, 0, 0), 0, NULL);
	zassert_equal(gui_horizontal_percentage_bar(40, 32, 360, 40, 1, 0, 37), 0, NULL);
	zassert_equal(gui_horizontal_percentage_bar(40, 44, 360, 52, 5, 8, 50), 0, NULL);
	zassert_equal(gui_horizontal_percentage_bar(40, 56, 360, 64, 4, 4, 100), 0, NULL);
	gui_expect_golden("horizontal_bars", GUI_PGM, 32, 16, 336, 52);
}

ZTEST(gui, test_horizontal_bar_rejects_bad_geometry)
{
	zassert_equal(gui_horizontal_percentage_bar(360, 20, 40, 28, 1, 0, 50), -1, NULL);
	zassert_equal(gui_horizontal_percentage_bar(40, 28, 360, 20, 1, 0, 50), -1, NULL);
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

/* bit 0 is the leftmost pixel */
ZTEST(gui, test_bitmap_draw_at_top_left_corner)
{
	static const uint8_t bmp[] = {0x01, 0x80, 0xff, 0x00};

	zassert_equal(gui_bitmap_draw(0, 0, 16, 2, bmp, 0xf), 0, NULL);

	zassert_equal(gui_test_px(0, 0), 15, NULL);
	zassert_equal(gui_test_px(15, 0), 15, NULL);
	zassert_equal(gui_test_lit(0, 0, 16, 1), 2, NULL);
	for (int x = 0; x < 8; x++) {
		zassert_equal(gui_test_px(x, 1), 15, "x %d", x);
	}
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 10, NULL);
}

ZTEST(gui, test_bitmap_draw_at_bottom_right_corner)
{
	static const uint8_t bmp[] = {0xff, 0xff, 0x55, 0xaa};
	static const int row199[] = {624, 626, 628, 630, 633, 635, 637, 639};

	zassert_equal(gui_bitmap_draw(624, 198, 640, 200, bmp, 0x3), 0, NULL);

	for (int x = 624; x < 640; x++) {
		zassert_equal(gui_test_px(x, 198), 3, "x %d", x);
	}
	for (size_t i = 0; i < ARRAY_SIZE(row199); i++) {
		zassert_equal(gui_test_px(row199[i], 199), 3, "x %d", row199[i]);
	}
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 16 + 8, NULL);
	zassert_true(gui_test_guard_intact(), NULL);
}

ZTEST(gui, test_bitmap_draw_replaces_background)
{
	static const uint8_t bmp[] = {0x01};

	for (int x = 0; x < 8; x++) {
		gui_test_px_set(x, 10, 0x9);
	}
	gui_bitmap_draw(0, 10, 8, 11, bmp, 0x2);

	zassert_equal(gui_test_px(0, 10), 2, NULL);
	zassert_equal(gui_test_lit(1, 10, 8, 11), 0, NULL);
}

ZTEST(gui, test_bitmap_draw_rejects_bad_parameters)
{
	static const uint8_t bmp[8] = {0xff};

	zassert_equal(gui_bitmap_draw(1, 0, 9, 1, bmp, 0xf), -1, "odd x0");
	zassert_equal(gui_bitmap_draw(16, 0, 8, 1, bmp, 0xf), -1, "x0 > x1");
	zassert_equal(gui_bitmap_draw(0, 2, 8, 1, bmp, 0xf), -1, "y0 > y1");
	zassert_equal(gui_bitmap_draw(0, 0, 8, 1, bmp, 0x10), -1, "level above 15");
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

/* Defect: rows past 199 are written through the row pointers. */
ZTEST_EXPECT_FAIL(gui, test_bitmap_draw_clips_at_bottom_edge);
ZTEST(gui, test_bitmap_draw_clips_at_bottom_edge)
{
	static const uint8_t bmp[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	gui_bitmap_draw(0, 196, 8, 204, bmp, 0xf);

	zassert_equal(gui_test_lit(0, 196, 8, 200), 32, NULL);
	zassert_true(gui_test_guard_intact(), "rows past 199 written");
}

/* Defect: a box past x 639 spills into the next row. */
ZTEST_EXPECT_FAIL(gui, test_bitmap_draw_clips_at_right_edge);
ZTEST(gui, test_bitmap_draw_clips_at_right_edge)
{
	static const uint8_t bmp[] = {0xff, 0xff};

	gui_bitmap_draw(632, 10, 648, 11, bmp, 0xf);

	zassert_equal(gui_test_lit(632, 10, 640, 11), 8, NULL);
	zassert_equal(gui_test_lit(0, 11, GUI_SCREEN_WIDTH, 12), 0, "pixels of row 11 written");
}

ZTEST(gui, test_1bit_bitmap_override_ors_into_existing_pixels)
{
	static const uint8_t bmp[] = {0x01, 0x00};

	gui_test_px_set(624, 0, 0x8);
	gui_test_px_set(625, 0, 0x8);
	zassert_equal(gui_1bit_bitmap_override(624, 0, 640, 1, bmp, 0x3), 0, NULL);

	zassert_equal(gui_test_px(624, 0), 0xb, NULL);
	zassert_equal(gui_test_px(625, 0), 0x8, NULL);
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 2, NULL);
	zassert_true(gui_test_guard_intact(), NULL);
}

ZTEST(gui, test_1bit_bitmap_override_rejects_bad_parameters)
{
	static const uint8_t bmp[8] = {0xff};

	zassert_equal(gui_1bit_bitmap_override(3, 0, 11, 1, bmp, 0xf), -1, "odd x0");
	zassert_equal(gui_1bit_bitmap_override(0, 0, 8, 1, bmp, 0x10), -1, "level above 15");
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

ZTEST(gui, test_bitmaps_merge_draw_combines_two_levels)
{
	static const uint8_t bmp1[] = {0x0f};
	static const uint8_t bmp2[] = {0x3c};
	static const uint8_t exp[8] = {1, 1, 5, 5, 4, 4, 0, 0};

	zassert_equal(gui_bitmps_merge_draw(0, 199, 8, 200, bmp1, bmp2, 0x1, 0x4), 0, NULL);

	for (int x = 0; x < 8; x++) {
		zassert_equal(gui_test_px(x, 199), exp[x], "x %d", x);
	}
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 6, NULL);
	zassert_true(gui_test_guard_intact(), NULL);
}

ZTEST(gui, test_bitmaps_merge_draw_rejects_bad_parameters)
{
	static const uint8_t bmp[8] = {0xff};

	zassert_equal(gui_bitmps_merge_draw(1, 0, 9, 1, bmp, bmp, 1, 2), -1, "odd x0");
	zassert_equal(gui_bitmps_merge_draw(0, 0, 8, 1, bmp, bmp, 0x10, 2), -1, "level above 15");
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

#define FADE_STEPS 8
static int fade_lit[FADE_STEPS + 1];
static int fade_refreshes;

static int fade_snapshot(uint32_t row, uint32_t col, int x0, int y0, int x1, int y1)
{
	ARG_UNUSED(row);
	ARG_UNUSED(col);
	zassert_equal(x0, 0, NULL);
	zassert_equal(y0, 0, NULL);
	zassert_equal(x1, GUI_SCREEN_WIDTH, NULL);
	zassert_equal(y1, 199, NULL);
	if (fade_refreshes < FADE_STEPS) {
		fade_lit[fade_refreshes] = gui_test_lit(0, 0, GUI_SCREEN_WIDTH, 199);
	}
	fade_refreshes++;
	return 0;
}

static void fade_full_screen(void)
{
	memset(gui_test_rows[0], 0xff, GUI_FB_ROWS * GUI_FB_ROW_BYTES);
	fade_refreshes = 0;
	memset(fade_lit, 0, sizeof(fade_lit));
	_reflash_fb_data_to_lcd_fake.custom_fake = fade_snapshot;
	g1_state()->panel.init_done = 1;
	zassert_equal(gui_screen_fade_out(), 0, NULL);
}

ZTEST(gui, test_fade_out_darkens_in_eight_steps_then_clears_panel)
{
	fade_full_screen();

	zassert_equal(fade_refreshes, FADE_STEPS, NULL);
	for (int i = 1; i < FADE_STEPS; i++) {
		zassert_true(fade_lit[i] <= fade_lit[i - 1], "step %d: %d lit after %d", i,
			     fade_lit[i], fade_lit[i - 1]);
	}
	zassert_true(fade_lit[FADE_STEPS / 2] < fade_lit[0], "no dimming half way");
	zassert_equal(fade_lit[FADE_STEPS - 1], 0, NULL);
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, 199), 0, NULL);
	zassert_equal(jbd_clear_cache_sync_fake.call_count, 1, NULL);
	zassert_equal(jbd_clear_cache_sync_fake.arg0_val, 0, NULL);
}

ZTEST(gui, test_fade_out_never_lights_pixels)
{
	g1_state()->panel.init_done = 1;
	gui_screen_fade_out();
	zassert_equal(gui_test_lit(0, 0, GUI_SCREEN_WIDTH, GUI_FB_ROWS), 0, NULL);
}

/* Defect: the loop stops at row < 199, so row 199 is never faded. */
ZTEST_EXPECT_FAIL(gui, test_fade_out_darkens_last_row);
ZTEST(gui, test_fade_out_darkens_last_row)
{
	fade_full_screen();
	zassert_equal(gui_test_lit(0, 199, GUI_SCREEN_WIDTH, 200), 0, "row 199 still lit");
}

/* 1 px per code point in the built-in font, at most 2 */
ZTEST(gui, test_font_kerning_pairs)
{
	zassert_equal(gui_font_kerning('A', 'B'), 2, "two built-in glyphs");
	zassert_equal(gui_font_kerning('A', CJK_ZHONG), 1, "built-in then CJK");
	zassert_equal(gui_font_kerning(CJK_ZHONG, 'A'), 1, "CJK then built-in");
	zassert_equal(gui_font_kerning(CJK_ZHONG, CJK_RI), 0, "two CJK glyphs");
	zassert_equal(gui_font_kerning('A', 0), 1, "last character of a string");
	zassert_equal(gui_font_kerning(0x0416, 'a'), 2, "Cyrillic Zhe then Latin");
}

ZTEST(gui, test_screen_x_offset_is_canvas_x_plus_panel_offset)
{
	g1_state()->canvas_x = 40;
	g1_state()->canvas_y = 30;
	g1_state()->panel.y_offset = 5;

	zassert_equal(gui_screen_x_offset(), 45, NULL);
	zassert_equal(gui_screen_y_offset(), 30, NULL);
}

ZTEST(gui, test_screen_x_offset_defaults_to_32)
{
	g1_state()->canvas_x = 0;
	g1_state()->canvas_y = 0;
	g1_state()->panel.y_offset = 3;

	zassert_equal(gui_screen_x_offset(), 35, NULL);
	zassert_equal(g1_state()->canvas_x, 32, "the default is stored");
}

ZTEST(gui, test_screen_y_offset_defaults_to_32)
{
	g1_state()->canvas_x = 0;
	g1_state()->canvas_y = 0;

	zassert_equal(gui_screen_y_offset(), 32, NULL);
	zassert_equal(g1_state()->canvas_y, 32, "the default is stored");
}

ZTEST(gui, test_screen_offsets_out_of_range)
{
	g1_state()->panel.y_offset = 0;
	g1_state()->canvas_x = 65;
	g1_state()->canvas_y = 64;
	zassert_equal(gui_screen_x_offset(), 32, NULL);
	zassert_equal(gui_screen_y_offset(), 64, NULL);

	g1_state()->canvas_x = 64;
	g1_state()->canvas_y = 65;
	zassert_equal(gui_screen_x_offset(), 64, NULL);
	zassert_equal(gui_screen_y_offset(), 32, NULL);

	g1_state()->canvas_y = 2;
	zassert_equal(gui_screen_y_offset(), 4, NULL);
}

/* Defect: x stores its default but y does not, so the result depends on call order. */
ZTEST_EXPECT_FAIL(gui, test_screen_offsets_unset_canvas_independent_of_call_order);
ZTEST(gui, test_screen_offsets_unset_canvas_independent_of_call_order)
{
	int x, y;

	g1_state()->panel.y_offset = 0;
	g1_state()->canvas_x = 0;
	g1_state()->canvas_y = 0;
	x = gui_screen_x_offset();
	y = gui_screen_y_offset();
	zassert_equal(x, 32, NULL);
	zassert_equal(y, 32, "x then y: y = %d", y);

	g1_state()->canvas_x = 0;
	g1_state()->canvas_y = 0;
	y = gui_screen_y_offset();
	x = gui_screen_x_offset();
	zassert_equal(y, 32, NULL);
	zassert_equal(x, 32, "y then x: x = %d", x);
}
