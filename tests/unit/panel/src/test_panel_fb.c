/*
 * Frame buffer helpers. panel_buffer[200] aliases row 0 and [201] current_row. _clean_fb_data()
 * ends are inclusive.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

static uint8_t before_img[FRAME_BYTES];

static uint8_t *row(int y)
{
	return panel_frame() + y * JBD_ROW_BYTES;
}

static void fill(uint8_t v)
{
	memset(panel_frame(), v, FRAME_BYTES);
}

static void fill_pattern(void)
{
	for (int i = 0; i < FRAME_BYTES; i++) {
		panel_frame()[i] = (uint8_t)(i * 13 + i / JBD_ROW_BYTES);
	}
	memcpy(before_img, panel_frame(), FRAME_BYTES);
}

ZTEST_SUITE(panel_fb, NULL, NULL, NULL, NULL, NULL);

ZTEST(panel_fb, test_frame_rows_are_320_bytes_apart)
{
	struct jbd_panel_context *c = panel_ctx();

	for (int y = 0; y < FRAME_ROWS; y++) {
		zassert_equal_ptr(c->panel_buffer[y], panel_frame() + y * JBD_ROW_BYTES, "row %d", y);
	}
	zassert_equal(c->current_buffer_size, FRAME_BYTES);
	zassert_equal_ptr(get_jdb_panel_context(), &g1_state()->panel);
}

ZTEST(panel_fb, test_clean_clears_inclusive_rows_and_byte_columns)
{
	fill(0xff);

	zassert_equal(_clean_fb_data(panel_fb(), 0x00, 20, 5, 41, 7), 11);

	for (int y = 4; y <= 8; y++) {
		for (int b = 9; b <= 21; b++) {
			bool inside = y >= 5 && y <= 7 && b >= 10 && b <= 20;

			zassert_equal(row(y)[b], inside ? 0x00 : 0xff, "row %d byte %d", y, b);
		}
	}
}

ZTEST(panel_fb, test_clean_widens_odd_pixels_to_whole_bytes)
{
	fill(0xff);

	zassert_equal(_clean_fb_data(panel_fb(), 0x00, 21, 5, 40, 5), 11);

	zassert_equal(row(5)[9], 0xff);
	zassert_equal(row(5)[10], 0x00);
	zassert_equal(row(5)[20], 0x00);
	zassert_equal(row(5)[21], 0xff);
}

ZTEST(panel_fb, test_clean_rejects_inverted_ranges)
{
	fill_pattern();

	zassert_equal(_clean_fb_data(panel_fb(), 0x00, 30, 5, 20, 7), -1);
	zassert_equal(_clean_fb_data(panel_fb(), 0x00, 0, 9, 10, 8), -1);
	zassert_equal(_clean_fb_data(panel_fb(), 0x00, 0, 250, 10, 300), -1);

	zassert_mem_equal(panel_frame(), before_img, FRAME_BYTES);
}

/* Defect: x_end 640 is inclusive here, so the clear spills into the next row. */
ZTEST_EXPECT_FAIL(panel_fb, test_clean_to_the_right_edge_stays_in_its_rows);
ZTEST(panel_fb, test_clean_to_the_right_edge_stays_in_its_rows)
{
	int ret;

	fill(0xff);

	ret = _clean_fb_data(panel_fb(), 0x00, 600, 10, 640, 20);

	zassert_equal(row(21)[0], 0xff, "row 21 byte 0 cleared");
	zassert_equal(row(20)[319], 0x00);
	zassert_equal(ret, 20);
}

ZTEST_EXPECT_FAIL(panel_fb, test_clean_past_the_bottom_stays_in_the_frame);
ZTEST(panel_fb, test_clean_past_the_bottom_stays_in_the_frame)
{
	fill(0xff);

	zassert_equal(_clean_fb_data(panel_fb(), 0x00, 0, 195, 9, 250), 5);

	zassert_equal(row(199)[0], 0x00);
	for (int b = 0; b < 5; b++) {
		zassert_equal(row(0)[b], 0xff, "row 0 byte %d cleared", b);
	}
}

ZTEST(panel_fb, test_copy_places_w_bytes_per_row_at_x_y)
{
	static const uint8_t src[3][4] = {
		{0x11, 0x22, 0x33, 0x44}, {0x55, 0x66, 0x77, 0x88}, {0x99, 0xaa, 0xbb, 0xcc}};

	fill(0xff);

	zassert_equal(copy_to_frame_buffer(panel_fb(), &src[0][0], 4, 3, 100, 50), 50);

	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(&row(50 + i)[50], src[i], 4, "row %d", 50 + i);
		zassert_equal(row(50 + i)[49], 0xff);
		zassert_equal(row(50 + i)[54], 0xff);
	}
	zassert_equal(row(49)[50], 0xff);
	zassert_equal(row(53)[50], 0xff);
}

ZTEST(panel_fb, test_copy_at_an_odd_x_starts_on_the_byte_of_x_minus_1)
{
	static const uint8_t src[2] = {0x12, 0x34};

	fill(0xff);

	copy_to_frame_buffer(panel_fb(), src, 2, 1, 101, 7);

	zassert_equal(row(7)[49], 0xff);
	zassert_mem_equal(&row(7)[50], src, 2);
}

ZTEST_EXPECT_FAIL(panel_fb, test_copy_at_the_right_edge_stays_in_its_row);
ZTEST(panel_fb, test_copy_at_the_right_edge_stays_in_its_row)
{
	static const uint8_t src[4] = {0x12, 0x34, 0x56, 0x78};

	fill(0xff);

	copy_to_frame_buffer(panel_fb(), src, 4, 1, 636, 10);

	zassert_equal(row(11)[0], 0xff, "row 11 written");
	zassert_equal(row(11)[1], 0xff, "row 11 written");
}

ZTEST_EXPECT_FAIL(panel_fb, test_copy_below_the_frame_draws_nothing);
ZTEST(panel_fb, test_copy_below_the_frame_draws_nothing)
{
	static const uint8_t src[4] = {0x12, 0x34, 0x56, 0x78};

	fill_pattern();

	copy_to_frame_buffer(panel_fb(), src, 4, 1, 100, 230);

	zassert_mem_equal(panel_frame(), before_img, FRAME_BYTES, "frame changed");
}

/* Crashes: the second row pointer is current_row, 0 here. */
ZTEST(panel_fb, test_copy_of_two_rows_below_the_frame_draws_nothing)
{
	static const uint8_t src[2][4] = {{0x12, 0x34, 0x56, 0x78}, {0x12, 0x34, 0x56, 0x78}};

	ztest_test_skip();

	fill_pattern();
	copy_to_frame_buffer(panel_fb(), &src[0][0], 4, 2, 100, 230);
	zassert_mem_equal(panel_frame(), before_img, FRAME_BYTES, "frame changed");
}

ZTEST(panel_fb, test_copy_raw_takes_x_as_a_byte_column)
{
	static const uint8_t src[2][3] = {{0x01, 0x02, 0x03}, {0x04, 0x05, 0x06}};

	fill(0xff);

	zassert_equal(copy_to_frame_buffer_raw(panel_fb(), &src[0][0], 3, 2, 32, 12), 12);

	zassert_mem_equal(&row(12)[32], src[0], 3);
	zassert_mem_equal(&row(13)[32], src[1], 3);
	zassert_equal(row(12)[31], 0xff);
	zassert_equal(row(12)[35], 0xff);
}

ZTEST(panel_fb, test_or_merges_w_pixels_per_row)
{
	static const uint8_t src[2][4] = {{0xa0, 0xa0, 0xa0, 0xa0}, {0x50, 0x50, 0x50, 0x50}};

	fill(0x0f);

	zassert_equal(or_to_frame_buffer(panel_fb(), &src[0][0], 8, 2, 100, 20), 20);

	for (int b = 50; b < 54; b++) {
		zassert_equal(row(20)[b], 0xaf, "row 20 byte %d", b);
		zassert_equal(row(21)[b], 0x5f, "row 21 byte %d", b);
	}
	zassert_equal(row(20)[49], 0x0f);
	zassert_equal(row(20)[54], 0x0f);
	zassert_equal(row(22)[50], 0x0f);
}

ZTEST_EXPECT_FAIL(panel_fb, test_or_at_the_right_edge_stays_in_its_row);
ZTEST(panel_fb, test_or_at_the_right_edge_stays_in_its_row)
{
	static const uint8_t src[4] = {0xa0, 0xa0, 0xa0, 0xa0};

	fill(0x0f);

	or_to_frame_buffer(panel_fb(), src, 8, 1, 636, 10);

	zassert_equal(row(11)[0], 0x0f, "row 11 written");
	zassert_equal(row(11)[1], 0x0f, "row 11 written");
}

ZTEST(panel_fb, test_copy_rows_moves_a_block)
{
	fill_pattern();

	copy_frame_buffer_rows(panel_fb(), 100, 50, 20, 10, 3, 40);

	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(&row(50 + i)[50], &before_img[(10 + i) * JBD_ROW_BYTES + 10], 20,
				  "row %d", 50 + i);
		zassert_equal(row(50 + i)[49], before_img[(50 + i) * JBD_ROW_BYTES + 49]);
		zassert_equal(row(50 + i)[70], before_img[(50 + i) * JBD_ROW_BYTES + 70]);
	}
	zassert_mem_equal(&row(10)[10], &before_img[10 * JBD_ROW_BYTES + 10], 20, "source changed");
}

ZTEST(panel_fb, test_copy_rows_scrolls_up_by_one_row)
{
	fill_pattern();

	copy_frame_buffer_rows(panel_fb(), 0, 10, 0, 11, 5, 640);

	for (int i = 0; i < 5; i++) {
		zassert_mem_equal(row(10 + i), &before_img[(11 + i) * JBD_ROW_BYTES], JBD_ROW_BYTES,
				  "row %d", 10 + i);
	}
}

/* Defect: rows are copied top-down, which breaks overlapping moves down. */
ZTEST_EXPECT_FAIL(panel_fb, test_copy_rows_scrolls_down_by_one_row);
ZTEST(panel_fb, test_copy_rows_scrolls_down_by_one_row)
{
	fill_pattern();

	copy_frame_buffer_rows(panel_fb(), 0, 11, 0, 10, 5, 640);

	for (int i = 0; i < 5; i++) {
		zassert_mem_equal(row(11 + i), &before_img[(10 + i) * JBD_ROW_BYTES], JBD_ROW_BYTES,
				  "row %d", 11 + i);
	}
}

ZTEST(panel_fb, test_clear_panel_zeroes_exactly_the_frame)
{
	struct jbd_panel_context *c = panel_ctx();
	uint8_t *after = panel_frame() + FRAME_BYTES;

	fill(0xff);
	memset(after, 0xa5, 3);

	c->clear_cb(c);

	for (int i = 0; i < FRAME_BYTES; i++) {
		zassert_equal(panel_frame()[i], 0x00, "frame byte %d", i);
	}
	zassert_mem_equal(after, ((uint8_t[]){0xa5, 0xa5, 0xa5}), 3);
}
