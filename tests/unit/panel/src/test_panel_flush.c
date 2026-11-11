/*
 * Frame buffer flush: one 02 cache write per row, then a 97 sync. x_end is exclusive, y_end
 * inclusive.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

static uint8_t snapshot[5 + FRAME_BYTES + 3];

static void fill_frame_pattern(void)
{
	uint8_t *f = panel_frame();

	for (int i = 0; i < FRAME_BYTES; i++) {
		f[i] = (uint8_t)(i * 7 + i / JBD_ROW_BYTES);
	}
}

static void panel_flush_before(void *f)
{
	ARG_UNUSED(f);
	panel_running();
	fill_frame_pattern();
}

ZTEST_SUITE(panel_flush, NULL, NULL, panel_flush_before, NULL, NULL);

static void expect_row_window(int n, const char *hdr, int from, size_t n_bytes)
{
	const struct g1_spim_entry *e = panel_xfer(n);
	uint8_t want[5];

	zassert_not_null(e, "no transfer %d", n);
	zassert_not_null(e->mosi);
	zassert_equal(g1_hex_parse(hdr, want, sizeof(want)), 5);
	zassert_mem_equal(e->mosi, want, 5, "transfer %d header", n);
	zassert_true(e->len >= 5 + n_bytes, "transfer %d: %zu bytes", n, e->len);
	zassert_mem_equal(e->mosi + 5, panel_frame() + from, n_bytes, "transfer %d pixels", n);
}

ZTEST(panel_flush, test_flush_is_refused_before_panel_init)
{
	panel_ctx()->init_done = 0;

	zassert_equal(_reflash_fb_data_to_lcd(0, 0, 0, 0, 640, 199), -1);

	zassert_equal(panel_xfer_count(), 0);
}

ZTEST(panel_flush, test_flush_sends_one_window_per_row_then_syncs)
{
	/* address (y << 10) | x */
	zassert_equal(_reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 300, 12), 0);

	zassert_equal(panel_xfer_count(), 4);
	expect_row_window(0, "02 00 68 84 ff", 10 * JBD_ROW_BYTES + 50, 100);
	expect_row_window(1, "02 00 6c 84 ff", 11 * JBD_ROW_BYTES + 50, 100);
	expect_row_window(2, "02 00 70 84 ff", 12 * JBD_ROW_BYTES + 50, 100);
	expect_xfer(3, "97");
}

ZTEST(panel_flush, test_full_frame_flush_sends_200_rows_of_320_bytes)
{
	zassert_equal(_reflash_fb_data_to_lcd(0, 0, 0, 0, 640, 199), 0);

	zassert_equal(panel_xfer_count(), FRAME_ROWS + 1);
	for (int y = 0; y < FRAME_ROWS; y++) {
		char hdr[32];
		uint32_t addr = (uint32_t)y << 10;

		snprintf(hdr, sizeof(hdr), "02 %02x %02x %02x ff", (addr >> 16) & 0xff,
			 (addr >> 8) & 0xff, addr & 0xff);
		expect_row_window(y, hdr, y * JBD_ROW_BYTES, JBD_ROW_BYTES);
	}
	expect_row_window(199, "02 03 1c 00 ff", 199 * JBD_ROW_BYTES, JBD_ROW_BYTES);
	expect_xfer(FRAME_ROWS, "97");
}

ZTEST(panel_flush, test_flush_leaves_the_frame_buffer_as_it_was)
{
	uint8_t *start = panel_frame() - 5;

	memcpy(snapshot, start, sizeof(snapshot));

	zassert_equal(_reflash_fb_data_to_lcd(0, 0, 0, 0, 640, 199), 0);
	zassert_equal(_reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 301, 12), 0);

	zassert_mem_equal(start, snapshot, sizeof(snapshot));
}

/* Defect: no trailing dummy clocks, so the panel drops each row's last pixel. */
ZTEST_EXPECT_FAIL(panel_flush, test_row_window_ends_with_the_dummy_clocks);
ZTEST(panel_flush, test_row_window_ends_with_the_dummy_clocks)
{
	zassert_equal(_reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 300, 10), 0);

	zassert_equal(panel_xfer(0)->len, 5 + 100 + 1, "row window of %zu bytes for 100 pixel bytes",
		      panel_xfer(0)->len);
}

/* Defect: y_end is clipped to 200, not 199, so row 200 is read past the frame buffer. */
ZTEST_EXPECT_FAIL(panel_flush, test_flush_stops_at_the_last_frame_row);
ZTEST(panel_flush, test_flush_stops_at_the_last_frame_row)
{
	zassert_equal(_reflash_fb_data_to_lcd(0, 0, 0, 195, 20, 250), 0);

	zassert_equal(panel_xfer_count(), 6, "%d transfers", panel_xfer_count());
}

ZTEST(panel_flush, test_flush_ex_reads_rows_shifted_by_offset)
{
	zassert_equal(_reflash_fb_data_to_lcd_ex(0x20, 0x10, 100, 10, 300, 11, 5), 0);

	zassert_equal(panel_xfer_count(), 3);
	expect_row_window(0, "02 00 68 84 ff", 15 * JBD_ROW_BYTES + 50, 100);
	expect_row_window(1, "02 00 6c 84 ff", 16 * JBD_ROW_BYTES + 50, 100);
	expect_xfer(2, "97");
}

ZTEST(panel_flush, test_a_failed_row_does_not_stop_the_flush)
{
	uint8_t *start = panel_frame() - 5;

	memcpy(snapshot, start, sizeof(snapshot));
	g1_fake_spim.xfer_error = NRFX_ERROR_BUSY;
	g1_fake_spim.xfer_error_skip = 1;
	g1_fake_spim.xfer_error_count = 1;

	(void)_reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 300, 12);

	zassert_equal(panel_xfer_count(), 4);
	zassert_equal(panel_xfer(1)->result, NRFX_ERROR_BUSY);
	expect_row_window(2, "02 00 70 84 ff", 12 * JBD_ROW_BYTES + 50, 100);
	expect_xfer(3, "97");
	zassert_mem_equal(start, snapshot, sizeof(snapshot), "frame buffer not restored");
}
