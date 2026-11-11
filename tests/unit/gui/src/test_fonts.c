/* UTF-8 decoding, 1 bpp to 4 bpp expansion and CJK glyph lookup. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "gui_test.h"
#include "fonts/font_bitmap.h"

static uint16_t dec(const char *s, int len)
{
	return utf8_decode_char((const uint8_t *)s, len);
}

ZTEST(gui, test_utf8_decode_two_byte_sequences)
{
	zassert_equal(dec("\xc3\xa9", 2), 0x00e9, "e acute");
	zassert_equal(dec("\xd0\xb6", 2), 0x0436, "Cyrillic zhe");
	zassert_equal(dec("\xc2\x80", 2), 0x0080, "lowest 2-byte code point");
	zassert_equal(dec("\xdf\xbf", 2), 0x07ff, "highest 2-byte code point");
}

ZTEST(gui, test_utf8_decode_three_byte_sequences)
{
	zassert_equal(dec("\xe4\xb8\xad", 3), 0x4e2d, "CJK zhong");
	zassert_equal(dec("\xe2\x82\xac", 3), 0x20ac, "euro sign");
	zassert_equal(dec("\xe2\x80\xa6", 3), 0x2026, "horizontal ellipsis");
	zassert_equal(dec("\xe0\xa0\x80", 3), 0x0800, "lowest 3-byte code point");
	zassert_equal(dec("\xef\xbf\xbd", 3), 0xfffd, "replacement character");
}

/* ASCII is handled by utf8_to_unicode() */
ZTEST(gui, test_utf8_decode_single_byte_returns_0)
{
	zassert_equal(dec("A", 1), 0, NULL);
	zassert_equal(dec("\x7f", 1), 0, NULL);
}

ZTEST(gui, test_utf8_decode_four_byte_sequence_returns_0)
{
	zassert_equal(dec("\xf0\x9f\x98\x80", 4), 0, "U+1F600");
}

ZTEST(gui, test_utf8_decode_invalid_bytes_return_0)
{
	zassert_equal(dec("\x80\x80", 2), 0, "lone continuation byte");
	zassert_equal(dec("\xbf", 1), 0, "lone continuation byte");
	zassert_equal(dec("\xff\x80\x80", 3), 0, "0xff is never a lead byte");
	zassert_equal(dec("\xe4\xb8\xad", 2), 0, "3-byte lead with only 2 bytes");
	zassert_equal(dec("\xc3\xa9", 1), 0, "2-byte lead with only 1 byte");
}

ZTEST(gui, test_utf8_to_unicode_mixed_text)
{
	uint16_t n = 0;
	uint16_t *u = utf8_to_unicode_buf("A\xc3\xa9\xe4\xb8\xad\n", &n);

	zassert_equal(n, 4, NULL);
	zassert_equal(u[0], 'A', NULL);
	zassert_equal(u[1], 0x00e9, NULL);
	zassert_equal(u[2], 0x4e2d, NULL);
	zassert_equal(u[3], '\n', NULL);
	zassert_equal(u[4], 0, "terminated");
}

ZTEST(gui, test_utf8_to_unicode_drops_control_characters)
{
	uint16_t n = 0;
	uint16_t *u = utf8_to_unicode_buf("a\tb\x01" "c\r\nd", &n);

	zassert_equal(n, 6, NULL);
	zassert_equal(u[0], 'a', NULL);
	zassert_equal(u[1], 'b', NULL);
	zassert_equal(u[2], 'c', NULL);
	zassert_equal(u[3], '\r', NULL);
	zassert_equal(u[4], '\n', NULL);
	zassert_equal(u[5], 'd', NULL);
}

ZTEST(gui, test_utf8_to_unicode_drops_four_byte_sequences)
{
	uint16_t n = 0;
	uint16_t *u = utf8_to_unicode_buf("a\xf0\x9f\x98\x80" "b", &n);

	zassert_equal(n, 2, NULL);
	zassert_equal(u[0], 'a', NULL);
	zassert_equal(u[1], 'b', NULL);
}

/* the buffer holds 0x400 code units */
ZTEST(gui, test_utf8_to_unicode_truncates_at_buffer_size)
{
	static char s[1501];
	uint16_t n = 0;
	uint16_t *u;

	memset(s, 'x', 1500);
	s[1500] = '\0';
	u = utf8_to_unicode_buf(s, &n);

	zassert_equal(n, 1023, NULL);
	zassert_equal(u[1022], 'x', NULL);
	zassert_equal(u[1023], 0, NULL);
}

ZTEST(gui, test_utf8_to_unicode_1023_code_points_fit)
{
	static char s[1024];
	uint16_t n = 0;
	uint16_t *u;

	memset(s, 'y', 1023);
	s[1023] = '\0';
	u = utf8_to_unicode_buf(s, &n);

	zassert_equal(n, 1023, NULL);
	zassert_equal(u[1022], 'y', NULL);
}

/* Defect: a 3-byte lead is taken without checking its continuation bytes. */
ZTEST_EXPECT_FAIL(gui, test_utf8_to_unicode_truncated_sequence_keeps_following_ascii);
ZTEST(gui, test_utf8_to_unicode_truncated_sequence_keeps_following_ascii)
{
	uint16_t n = 0;
	uint16_t *u = utf8_to_unicode_buf("\xe4\xb8" "AB", &n);
	bool has_a = false;

	for (int i = 0; i < n; i++) {
		has_a |= u[i] == 'A';
	}
	zassert_true(has_a, "got %d code points: %04x %04x", n, u[0], u[1]);
	zassert_equal(u[n - 1], 'B', NULL);
}

ZTEST(gui, test_expand_1bpp_writes_four_bytes_per_source_byte)
{
	static const uint8_t src[] = {0xff, 0x00, 0xff};
	uint8_t dst[16];

	memset(dst, 0x77, sizeof(dst));
	expand_1bpp_to_4bpp(src, 3, dst);

	for (int i = 0; i < 4; i++) {
		zassert_equal(dst[i], 0xff, NULL);
		zassert_equal(dst[4 + i], 0x00, NULL);
		zassert_equal(dst[8 + i], 0xff, NULL);
		zassert_equal(dst[12 + i], 0x77, "byte %d written past the output", 12 + i);
	}
}

/* Defect (latent): bit 7 goes to the low nibble, but the left pixel is the high one. */
ZTEST_EXPECT_FAIL(gui, test_expand_1bpp_msb_is_left_pixel_in_high_nibble);
ZTEST(gui, test_expand_1bpp_msb_is_left_pixel_in_high_nibble)
{
	static const uint8_t src[] = {0x80, 0x01, 0xa5};
	static const uint8_t exp[] = {0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
				      0xf0, 0xf0, 0x0f, 0x0f};
	uint8_t dst[12];

	expand_1bpp_to_4bpp(src, 3, dst);
	zassert_mem_equal(dst, exp, sizeof(exp), "got %02x %02x %02x %02x", dst[0], dst[1], dst[2],
			  dst[3]);
}

/*
 * The countdown page ("时间到!") and the template page (":YPR", digits) draw ASCII through
 * this lookup and copy the glyph rows from the result.
 * Defect: font_ascii_lookup() is a stub that returns NULL, and the callers copy from it.
 */
ZTEST_EXPECT_FAIL(gui, test_find_chinese_bitmap_gives_printable_ascii_a_glyph);
ZTEST(gui, test_find_chinese_bitmap_gives_printable_ascii_a_glyph)
{
	for (int c = 0x20; c <= 0x7e; c++) {
		zassert_not_null(find_chinese_bitmap_by_unicode(c, 12, 36), "code 0x%02x '%c'", c,
				 c);
	}
}

/* Defect, skipped (corrupts memory): 12 x 36 expands 1728 bytes into the 432-byte buffer. */
ZTEST(gui, test_find_chinese_bitmap_expands_glyph_within_buffer)
{
	ztest_test_skip();

	zassert_not_null(find_chinese_bitmap_by_unicode(CJK_ZHONG, 12, 36), NULL);
}

ZTEST(gui, test_check_printable_code)
{
	zassert_equal(check_printable_code(' '), 0, NULL);
	zassert_equal(check_printable_code(0x4e2d), 0, NULL);
	zassert_equal(check_printable_code('\n'), -1, NULL);
	zassert_equal(check_printable_code(0x1f), -1, NULL);
}
