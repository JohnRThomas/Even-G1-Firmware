/*
 * Frame buffer helpers. panel_buffer[200] aliases row 0 and [201] current_row. _clean_fb_data()
 * ends are inclusive.
 */
#include "panel_support.h"

static uint8_t before_img[FRAME_BYTES];

static uint8_t **panel_fb(void)
{
  return panel_ctx()->panel_buffer;
}

static uint8_t *fb_row(int row)
{
  return panel_frame() + row * JBD_ROW_BYTES;
}

static void fill_frame(uint8_t value)
{
  memset(panel_frame(), value, FRAME_BYTES);
}

static void fill_pattern(void)
{
  for (int index = 0; index < FRAME_BYTES; index++) {
    panel_frame()[index] = (uint8_t)(index * 13 + index / JBD_ROW_BYTES);
  }
  memcpy(before_img, panel_frame(), FRAME_BYTES);
}

ZTEST_SUITE(panel_fb, NULL, NULL, NULL, NULL, NULL);

ZTEST(panel_fb, test_frame_rows_are_320_bytes_apart)
{
  struct jbd_panel_context *panel = panel_ctx();

  for (int row = 0; row < FRAME_ROWS; row++) {
    zassert_equal_ptr(panel->panel_buffer[row], fb_row(row), "row %d", row);
  }
  zassert_equal(panel->current_buffer_size, FRAME_BYTES);
  zassert_equal_ptr(get_jdb_panel_context(), &g1_state()->panel);
}

ZTEST(panel_fb, test_clean_clears_inclusive_rows_and_byte_columns)
{
  fill_frame(0xff);

  zassert_equal(_clean_fb_data(panel_fb(), 0x00, 20, 5, 41, 7), 11);

  for (int row = 4; row <= 8; row++) {
    for (int col = 9; col <= 21; col++) {
      bool inside = row >= 5 && row <= 7 && col >= 10 && col <= 20;

      zassert_equal(fb_row(row)[col], inside ? 0x00 : 0xff, "row %d byte %d",
              row, col);
    }
  }
}

ZTEST(panel_fb, test_clean_widens_odd_pixels_to_whole_bytes)
{
  fill_frame(0xff);

  zassert_equal(_clean_fb_data(panel_fb(), 0x00, 21, 5, 40, 5), 11);

  zassert_equal(fb_row(5)[9], 0xff);
  zassert_equal(fb_row(5)[10], 0x00);
  zassert_equal(fb_row(5)[20], 0x00);
  zassert_equal(fb_row(5)[21], 0xff);
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

  fill_frame(0xff);

  ret = _clean_fb_data(panel_fb(), 0x00, 600, 10, 640, 20);

  zassert_equal(fb_row(21)[0], 0xff, "row 21 byte 0 cleared");
  zassert_equal(fb_row(20)[319], 0x00);
  zassert_equal(ret, 20);
}

/* Defect: y_end is clipped to 200, not 199, and row 200 aliases row 0, so row 0 is cleared. */
ZTEST_EXPECT_FAIL(panel_fb, test_clean_past_the_bottom_stays_in_the_frame);
ZTEST(panel_fb, test_clean_past_the_bottom_stays_in_the_frame)
{
  fill_frame(0xff);

  zassert_equal(_clean_fb_data(panel_fb(), 0x00, 0, 195, 9, 250), 5);

  zassert_equal(fb_row(199)[0], 0x00);
  expect_fill(fb_row(0), 5, 0xff, "row 0");
}

ZTEST(panel_fb, test_copy_places_w_bytes_per_row_at_x_y)
{
  static const uint8_t src[3][4] = {
    {0x11, 0x22, 0x33, 0x44}, {0x55, 0x66, 0x77, 0x88}, {0x99, 0xaa, 0xbb, 0xcc}};

  fill_frame(0xff);

  zassert_equal(copy_to_frame_buffer(panel_fb(), &src[0][0], 4, 3, 100, 50), 50);

  for (int line = 0; line < 3; line++) {
    zassert_mem_equal(&fb_row(50 + line)[50], src[line], 4, "row %d", 50 + line);
    zassert_equal(fb_row(50 + line)[49], 0xff);
    zassert_equal(fb_row(50 + line)[54], 0xff);
  }
  zassert_equal(fb_row(49)[50], 0xff);
  zassert_equal(fb_row(53)[50], 0xff);
}

ZTEST(panel_fb, test_copy_at_an_odd_x_starts_on_the_byte_of_x_minus_1)
{
  static const uint8_t src[2] = {0x12, 0x34};

  fill_frame(0xff);

  copy_to_frame_buffer(panel_fb(), src, 2, 1, 101, 7);

  zassert_equal(fb_row(7)[49], 0xff);
  zassert_mem_equal(&fb_row(7)[50], src, 2);
}

/* Defect: the width is not clipped, so bytes past byte 319 land in the next row. */
ZTEST_EXPECT_FAIL(panel_fb, test_copy_at_the_right_edge_stays_in_its_row);
ZTEST(panel_fb, test_copy_at_the_right_edge_stays_in_its_row)
{
  static const uint8_t src[4] = {0x12, 0x34, 0x56, 0x78};

  fill_frame(0xff);

  copy_to_frame_buffer(panel_fb(), src, 4, 1, 636, 10);

  expect_fill(fb_row(11), 2, 0xff, "row 11");
}

/* Defect: y is clipped to 200, not skipped, and row 200 aliases row 0. */
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

  fill_frame(0xff);

  zassert_equal(copy_to_frame_buffer_raw(panel_fb(), &src[0][0], 3, 2, 32, 12), 12);

  zassert_mem_equal(&fb_row(12)[32], src[0], 3);
  zassert_mem_equal(&fb_row(13)[32], src[1], 3);
  zassert_equal(fb_row(12)[31], 0xff);
  zassert_equal(fb_row(12)[35], 0xff);
}

ZTEST(panel_fb, test_or_merges_w_pixels_per_row)
{
  static const uint8_t src[2][4] = {{0xa0, 0xa0, 0xa0, 0xa0}, {0x50, 0x50, 0x50, 0x50}};

  fill_frame(0x0f);

  zassert_equal(or_to_frame_buffer(panel_fb(), &src[0][0], 8, 2, 100, 20), 20);

  expect_fill(&fb_row(20)[50], 4, 0xaf, "row 20 bytes 50..53");
  expect_fill(&fb_row(21)[50], 4, 0x5f, "row 21 bytes 50..53");
  zassert_equal(fb_row(20)[49], 0x0f);
  zassert_equal(fb_row(20)[54], 0x0f);
  zassert_equal(fb_row(22)[50], 0x0f);
}

/* Defect: the width is not clipped, so bytes past byte 319 land in the next row. */
ZTEST_EXPECT_FAIL(panel_fb, test_or_at_the_right_edge_stays_in_its_row);
ZTEST(panel_fb, test_or_at_the_right_edge_stays_in_its_row)
{
  static const uint8_t src[4] = {0xa0, 0xa0, 0xa0, 0xa0};

  fill_frame(0x0f);

  or_to_frame_buffer(panel_fb(), src, 8, 1, 636, 10);

  expect_fill(fb_row(11), 2, 0x0f, "row 11");
}

ZTEST(panel_fb, test_copy_rows_moves_a_block)
{
  fill_pattern();

  copy_frame_buffer_rows(panel_fb(), 100, 50, 20, 10, 3, 40);

  for (int line = 0; line < 3; line++) {
    zassert_mem_equal(&fb_row(50 + line)[50],
          &before_img[(10 + line) * JBD_ROW_BYTES + 10], 20, "row %d",
          50 + line);
    zassert_equal(fb_row(50 + line)[49], before_img[(50 + line) * JBD_ROW_BYTES + 49]);
    zassert_equal(fb_row(50 + line)[70], before_img[(50 + line) * JBD_ROW_BYTES + 70]);
  }
  zassert_mem_equal(&fb_row(10)[10], &before_img[10 * JBD_ROW_BYTES + 10], 20,
        "source changed");
}

ZTEST(panel_fb, test_copy_rows_scrolls_up_by_one_row)
{
  fill_pattern();

  copy_frame_buffer_rows(panel_fb(), 0, 10, 0, 11, 5, 640);

  for (int line = 0; line < 5; line++) {
    zassert_mem_equal(fb_row(10 + line), &before_img[(11 + line) * JBD_ROW_BYTES],
          JBD_ROW_BYTES, "row %d", 10 + line);
  }
}

/* Defect: rows are copied top-down, which breaks overlapping moves down. */
ZTEST_EXPECT_FAIL(panel_fb, test_copy_rows_scrolls_down_by_one_row);
ZTEST(panel_fb, test_copy_rows_scrolls_down_by_one_row)
{
  fill_pattern();

  copy_frame_buffer_rows(panel_fb(), 0, 11, 0, 10, 5, 640);

  for (int line = 0; line < 5; line++) {
    zassert_mem_equal(fb_row(11 + line), &before_img[(10 + line) * JBD_ROW_BYTES],
          JBD_ROW_BYTES, "row %d", 11 + line);
  }
}

ZTEST(panel_fb, test_clear_panel_zeroes_exactly_the_frame)
{
  struct jbd_panel_context *panel = panel_ctx();
  uint8_t *after = panel_frame() + FRAME_BYTES;

  fill_frame(0xff);
  memset(after, 0xa5, 3);

  panel->clear_cb(panel);

  expect_fill(panel_frame(), FRAME_BYTES, 0x00, "frame");
  expect_bytes(after, 3, "a5 a5 a5");
}
