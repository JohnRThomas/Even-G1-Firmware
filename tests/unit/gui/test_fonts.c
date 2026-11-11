/*
 * UTF-8 decoding, 1 bpp to 4 bpp expansion, glyph lookup and the resource tables.
 * resource_manager.c is compiled in to reach its static tables. Resource types: 0 text, 1/2 clock
 * digits, 3 26 px ASCII, 4 icons, 5 animations.
 */
#include "gui_test.h"
#include "fonts/font_bitmap.h"

#include "gui/resource_manager.c"

ZTEST(gui, test_utf8_decode)
{
  static const struct {
    const char *bytes;
    int len;
    uint16_t code;
    const char *what;
  } decode[] = {
    {"\xc3\xa9", 2, 0x00e9, "e acute"},
    {"\xd0\xb6", 2, 0x0436, "Cyrillic zhe"},
    {"\xc2\x80", 2, 0x0080, "lowest 2-byte code point"},
    {"\xdf\xbf", 2, 0x07ff, "highest 2-byte code point"},
    {"\xe4\xb8\xad", 3, 0x4e2d, "CJK zhong"},
    {"\xe2\x82\xac", 3, 0x20ac, "euro sign"},
    {"\xe2\x80\xa6", 3, 0x2026, "horizontal ellipsis"},
    {"\xe0\xa0\x80", 3, 0x0800, "lowest 3-byte code point"},
    {"\xef\xbf\xbd", 3, 0xfffd, "replacement character"},
    /* ASCII is handled by utf8_to_unicode() */
    {"A", 1, 0, "ASCII A"},
    {"\x7f", 1, 0, "ASCII DEL"},
    {"\xf0\x9f\x98\x80", 4, 0, "4-byte U+1F600"},
    {"\x80\x80", 2, 0, "lone continuation byte 0x80"},
    {"\xbf", 1, 0, "lone continuation byte 0xbf"},
    {"\xff\x80\x80", 3, 0, "0xff is never a lead byte"},
    {"\xe4\xb8\xad", 2, 0, "3-byte lead with only 2 bytes"},
    {"\xc3\xa9", 1, 0, "2-byte lead with only 1 byte"},
  };

  for (size_t row = 0; row < ARRAY_SIZE(decode); row++) {
    zassert_equal(utf8_decode_char((const uint8_t *)decode[row].bytes, decode[row].len),
            decode[row].code, "%s", decode[row].what);
  }
}

/* utf8_to_unicode_buf() gives exactly the code units want[0..count); returns its buffer */
static uint16_t *expect_code_units(const char *text, const uint16_t *want, uint16_t count)
{
  uint16_t len = 0;
  uint16_t *units = utf8_to_unicode_buf(text, &len);

  zassert_equal(len, count);
  zassert_mem_equal(units, want, count * sizeof(*want));
  return units;
}

ZTEST(gui, test_utf8_to_unicode_mixed_text)
{
  static const uint16_t want[] = {'A', 0x00e9, 0x4e2d, '\n'};
  uint16_t *units = expect_code_units("A\xc3\xa9\xe4\xb8\xad\n", want, ARRAY_SIZE(want));

  zassert_equal(units[4], 0, "terminated");
}

ZTEST(gui, test_utf8_to_unicode_drops_control_characters)
{
  static const uint16_t want[] = {'a', 'b', 'c', '\r', '\n', 'd'};

  expect_code_units("a\tb\x01" "c\r\nd", want, ARRAY_SIZE(want));
}

ZTEST(gui, test_utf8_to_unicode_drops_four_byte_sequences)
{
  static const uint16_t want[] = {'a', 'b'};

  expect_code_units("a\xf0\x9f\x98\x80" "b", want, ARRAY_SIZE(want));
}

/* the buffer holds 0x400 code units */
ZTEST(gui, test_utf8_to_unicode_keeps_at_most_1023_code_points)
{
  static const struct {
    size_t len;
    char fill;
  } texts[] = {{1500, 'x'}, {1023, 'y'}};
  static char text[1501];

  for (size_t row = 0; row < ARRAY_SIZE(texts); row++) {
    uint16_t count = 0;
    uint16_t *units;

    memset(text, texts[row].fill, texts[row].len);
    text[texts[row].len] = '\0';
    units = utf8_to_unicode_buf(text, &count);

    zassert_equal(count, 1023, "%zu bytes", texts[row].len);
    zassert_equal(units[1022], texts[row].fill, "%zu bytes", texts[row].len);
    zassert_equal(units[1023], 0, "%zu bytes", texts[row].len);
  }
}

ZTEST(gui, test_utf8_to_unicode_truncated_sequence_keeps_following_ascii)
{
  uint16_t count = 0;
  uint16_t *units = utf8_to_unicode_buf("\xe4\xb8" "AB", &count);
  bool has_a = false;

  for (int index = 0; index < count; index++) {
    has_a |= units[index] == 'A';
  }
  zassert_true(has_a, "got %d code points: %04x %04x", count, units[0], units[1]);
  zassert_equal(units[count - 1], 'B');
}

ZTEST(gui, test_expand_1bpp_writes_four_bytes_per_source_byte)
{
  static const uint8_t src[] = {0xff, 0x00, 0xff};
  uint8_t dst[16];

  memset(dst, 0x77, sizeof(dst));
  expand_1bpp_to_4bpp(src, 3, dst);

  /* the last 4 bytes are past the output */
  expect_bytes(dst, sizeof(dst), "ffffffff 00000000 ffffffff 77777777");
}

ZTEST(gui, test_expand_1bpp_msb_is_left_pixel_in_high_nibble)
{
  static const uint8_t src[] = {0x80, 0x01, 0xa5};
  uint8_t dst[12];

  expand_1bpp_to_4bpp(src, 3, dst);
  expect_bytes(dst, sizeof(dst), "f0000000 0000000f f0f00f0f");
}

/*
 * The countdown page ("时间到!") and the template page (":YPR", digits) draw ASCII through
 * this lookup and copy the glyph rows from the result.
 */
ZTEST(gui, test_find_chinese_bitmap_gives_printable_ascii_a_glyph)
{
  for (int code = 0x20; code <= 0x7e; code++) {
    zassert_not_null(find_chinese_bitmap_by_unicode(code, 12, 36), "code 0x%02x '%c'",
         code, code);
  }
}

/* 12 x 36: 108 glyph bytes expand to the 432-byte buffer */
ZTEST(gui, test_find_chinese_bitmap_expands_glyph_within_buffer)
{
  zassert_not_null(find_chinese_bitmap_by_unicode(CJK_ZHONG, 12, 36));
}

ZTEST(gui, test_check_printable_code)
{
  zassert_equal(check_printable_code(' '), 0);
  zassert_equal(check_printable_code(0x4e2d), 0);
  zassert_equal(check_printable_code('\n'), -1);
  zassert_equal(check_printable_code(0x1f), -1);
}

/* resource_manger_get() returns 0 and a width x height resource; returns its data */
static const uint8_t *expect_resource(int type, int code, int width, int height)
{
  int got_width = 0, got_height = 0;
  const uint8_t *data = NULL;

  zassert_equal(resource_manger_get(type, code, &got_width, &got_height, &data, NULL), 0,
          "type %d code 0x%02x", type, code);
  zassert_equal(got_width, width, "type %d code 0x%02x", type, code);
  zassert_equal(got_height, height, "type %d code 0x%02x", type, code);
  return data;
}

/* the return value of resource_manger_get() */
static int get_resource(int type, int code)
{
  int width = 0, height = 0;
  const uint8_t *data = NULL;

  return resource_manger_get(type, code, &width, &height, &data, NULL);
}

ZTEST(gui, test_resource_font0_ascii_glyph_is_built_in)
{
  const uint8_t *data = expect_resource(0, 'A', 12, 26);

  zassert_true(data >= font26_bitmap && data < font26_bitmap + 52988,
         "not from font26_bitmap");
  zassert_equal(g1_fake_flash_read_fake.call_count, 0, "built-in glyphs need no flash read");
}

ZTEST(gui, test_resource_font0_cjk_glyph_read_from_flash)
{
  const uint8_t *data = expect_resource(0, CJK_ZHONG, 18, 26);

  zassert_mem_equal(data, gui_test_cjk_glyph(CJK_ZHONG), 9 * 26);
}

ZTEST(gui, test_resource_font0_missing_cjk_glyph_returns_error)
{
  zassert_equal(get_resource(0, CJK_ER), -1);
}

ZTEST(gui, test_resource_font0_flash_error_returns_error)
{
  G1_FAKE_RETURN(g1_fake_flash_read, -EIO);
  zassert_equal(get_resource(0, CJK_ZHONG), -1);
}

ZTEST(gui, test_resource_clock_digit_fonts)
{
  expect_resource(1, '0', 24, 32);
  expect_resource(1, ':', 4, 32);
  expect_resource(2, '1', 6, 39);
  expect_resource(2, '5', 18, 39);
  zassert_equal(get_resource(1, 'A'), -1);
  zassert_equal(get_resource(2, ' '), -1);
}

ZTEST(gui, test_resource_font3_ascii_glyph)
{
  zassert_not_null(expect_resource(3, 'A', 12, 26));
}

ZTEST(gui, test_resource_font3_missing_glyph_returns_error)
{
  int width = 0, height = 0;
  const uint8_t *data = NULL;

  zassert_equal(resource_manger_get(3, '!', &width, &height, &data, NULL), -1, "data %p",
          (void *)data);
}

ZTEST(gui, test_resource_icon_lookup)
{
  zassert_equal(expect_resource(4, 0x17, 10, 13), icon_scrollbar_thumb);
  zassert_equal(sizeof(icon_scrollbar_thumb), 10 / 2 * 13, "size matches the table");
}

ZTEST(gui, test_resource_icon_unknown_ids)
{
  zassert_equal(get_resource(4, 0x15), -1);
  zassert_equal(get_resource(4, 0x88), -1);
  zassert_equal(get_resource(4, 0x34), -1);
  zassert_equal(get_resource(4, 0x84), -1);
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

  for (size_t row = 0; row < ARRAY_SIZE(anims); row++) {
    int id = anims[row].id;
    int width = 0, height = 0;
    const uint8_t *data = NULL;
    uint8_t frames = 0;

    zassert_equal(resource_manger_get(5, id, &width, &height, &data, &frames), 0,
            "id %d", id);
    zassert_equal(data, anims[row].data, "id %d", id);
    zassert_equal((size_t)frames * (width / 2) * height, anims[row].size,
            "id %d: %d frames of %dx%d, table %u bytes", id, frames, width,
            height, (unsigned int)anims[row].size);
  }
  zassert_equal(get_resource(5, 13), -1);
  zassert_equal(get_resource(5, 0), -1);
}

ZTEST(gui, test_weather_icon_ids_map_to_icons_of_notes_table)
{
  static const uint8_t *const want[0x11] = {
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
    int resource = weather_icon_to_bitmap(id);

    zassert_equal(expect_resource(4, resource, 20, 26), want[id],
            "weather 0x%02x -> resource 0x%02x is the wrong icon", id, resource);
  }
}

ZTEST(gui, test_weather_icon_none_and_error_ids_have_no_icon)
{
  zassert_equal(weather_icon_to_bitmap(0x00), -1);
  zassert_equal(weather_icon_to_bitmap(0x11), -1);
  zassert_equal(weather_icon_to_bitmap(0xff), -1);
  zassert_equal(weather_icon_to_bitmap(-1), -1);
}
