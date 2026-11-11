/*
 * resource_manager.c is compiled in to reach its static tables. Types: 0 text, 1/2 clock digits, 3
 * 26 px ASCII, 4 icons, 5 animations.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_storage.h"
#include "gui_test.h"

#include "gui/resource_manager.c"

ZTEST(gui, test_resource_font0_ascii_glyph_is_built_in)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(0, 'A', &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 12, NULL);
	zassert_equal(h, 26, NULL);
	zassert_true(d >= font26_bitmap && d < font26_bitmap + 52988, "not from font26_bitmap");
	zassert_equal(g1_fake_flash_read_fake.call_count, 0, "built-in glyphs need no flash read");
}

ZTEST(gui, test_resource_font0_cjk_glyph_read_from_flash)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(0, CJK_ZHONG, &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 18, NULL);
	zassert_equal(h, 26, NULL);
	zassert_mem_equal(d, gui_test_cjk_glyph(CJK_ZHONG), 9 * 26, NULL);
}

ZTEST(gui, test_resource_font0_missing_cjk_glyph_returns_error)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(0, CJK_ER, &w, &h, &d, NULL), -1, NULL);
}

ZTEST(gui, test_resource_font0_flash_error_returns_error)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	g1_fake_flash_read_fake.custom_fake = NULL;
	g1_fake_flash_read_fake.return_val = -EIO;
	zassert_equal(resource_manger_get(0, CJK_ZHONG, &w, &h, &d, NULL), -1, NULL);
}

ZTEST(gui, test_resource_clock_digit_fonts)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(1, '0', &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 24, NULL);
	zassert_equal(h, 32, NULL);
	zassert_equal(resource_manger_get(1, ':', &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 4, NULL);
	zassert_equal(resource_manger_get(2, '1', &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 6, NULL);
	zassert_equal(h, 39, NULL);
	zassert_equal(resource_manger_get(2, '5', &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 18, NULL);
	zassert_equal(resource_manger_get(1, 'A', &w, &h, &d, NULL), -1, NULL);
	zassert_equal(resource_manger_get(2, ' ', &w, &h, &d, NULL), -1, NULL);
}

ZTEST(gui, test_resource_font3_ascii_glyph)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(3, 'A', &w, &h, &d, NULL), 0, NULL);
	zassert_equal(w, 12, NULL);
	zassert_equal(h, 26, NULL);
	zassert_not_null(d, NULL);
}

/* Defect: a missing glyph returns 0 with data NULL; callers then copy from NULL. */
ZTEST_EXPECT_FAIL(gui, test_resource_font3_missing_glyph_returns_error);
ZTEST(gui, test_resource_font3_missing_glyph_returns_error)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(3, '!', &w, &h, &d, NULL), -1, "data %p", (void *)d);
}

ZTEST(gui, test_resource_icon_lookup)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(4, 0x17, &w, &h, &d, NULL), 0, NULL);
	zassert_equal(d, icon_scrollbar_thumb, NULL);
	zassert_equal(w, 10, NULL);
	zassert_equal(h, 13, NULL);
	zassert_equal(w / 2 * h, sizeof(icon_scrollbar_thumb), "size matches the table");
}

ZTEST(gui, test_resource_icon_unknown_ids)
{
	int w = 0, h = 0;
	const uint8_t *d = NULL;

	zassert_equal(resource_manger_get(4, 0x15, &w, &h, &d, NULL), -1, NULL);
	zassert_equal(resource_manger_get(4, 0x88, &w, &h, &d, NULL), -1, NULL);
	zassert_equal(resource_manger_get(4, 0x34, &w, &h, &d, NULL), -1, NULL);
	zassert_equal(resource_manger_get(4, 0x84, &w, &h, &d, NULL), -1, NULL);
}

ZTEST(gui, test_resource_animations_frames_fill_their_tables)
{
	static const struct {
		int id;
		const uint8_t *data;
		size_t size;
	} anims[] = {
		{1, anim_listening, sizeof(anim_listening)},
		{2, anim_head_up_hint, sizeof(anim_head_up_hint)},
		{3, anim_head_down_hint, sizeof(anim_head_down_hint)},
		{4, anim_ai_answer, sizeof(anim_ai_answer)},
		{5, anim_message_popup, sizeof(anim_message_popup)},
		{6, anim_silent_mode_on, sizeof(anim_silent_mode_on)},
		{7, anim_silent_mode_off, sizeof(anim_silent_mode_off)},
		{8, anim_nav_route_generating, sizeof(anim_nav_route_generating)},
		{9, anim_processing, sizeof(anim_processing)},
		{10, anim_teleprompter, sizeof(anim_teleprompter)},
		{11, anim_single_tap_hint, sizeof(anim_single_tap_hint)},
		{12, anim_double_tap_hint, sizeof(anim_double_tap_hint)},
	};

	for (size_t i = 0; i < ARRAY_SIZE(anims); i++) {
		int w = 0, h = 0;
		const uint8_t *d = NULL;
		uint8_t frames = 0;

		zassert_equal(resource_manger_get(5, anims[i].id, &w, &h, &d, &frames), 0, "id %d",
			      anims[i].id);
		zassert_equal(d, anims[i].data, "id %d", anims[i].id);
		zassert_equal((size_t)frames * (w / 2) * h, anims[i].size,
			      "id %d: %d frames of %dx%d, table %u bytes", anims[i].id, frames, w, h,
			      (unsigned int)anims[i].size);
	}
	{
		int w, h;
		const uint8_t *d;
		uint8_t frames;

		zassert_equal(resource_manger_get(5, 13, &w, &h, &d, &frames), -1, NULL);
		zassert_equal(resource_manger_get(5, 0, &w, &h, &d, &frames), -1, NULL);
	}
}

ZTEST(gui, test_weather_icon_ids_map_to_icons_of_notes_table)
{
	static const uint8_t *const exp[0x11] = {
		[0x01] = icon_weather_night,      [0x02] = icon_weather_clouds,
		[0x03] = icon_weather_drizzle,    [0x04] = icon_weather_heavy_drizzle,
		[0x05] = icon_weather_rain,       [0x06] = icon_weather_heavy_rain,
		[0x07] = icon_weather_thunder,    [0x08] = icon_weather_thunderstorm,
		[0x09] = icon_weather_snow,       [0x0a] = icon_weather_mist,
		[0x0b] = icon_weather_fog,        [0x0c] = icon_weather_sand,
		[0x0d] = icon_weather_squalls,    [0x0e] = icon_weather_tornado,
		[0x0f] = icon_weather_freezing,   [0x10] = icon_weather_sunny,
	};

	for (int id = 1; id <= 0x10; id++) {
		int res = weather_icon_to_bitmap(id);
		int w = 0, h = 0;
		const uint8_t *d = NULL;

		zassert_equal(resource_manger_get(4, res, &w, &h, &d, NULL), 0, "weather 0x%02x", id);
		zassert_equal(d, exp[id], "weather 0x%02x -> resource 0x%02x is the wrong icon", id,
			      res);
		zassert_equal(w, 20, NULL);
		zassert_equal(h, 26, NULL);
	}
}

ZTEST(gui, test_weather_icon_none_and_error_ids_have_no_icon)
{
	zassert_equal(weather_icon_to_bitmap(0x00), -1, NULL);
	zassert_equal(weather_icon_to_bitmap(0x11), -1, NULL);
	zassert_equal(weather_icon_to_bitmap(0xff), -1, NULL);
	zassert_equal(weather_icon_to_bitmap(-1), -1, NULL);
}
