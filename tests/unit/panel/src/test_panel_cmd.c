/*
 * JBD013 command layer. Every command is its own CSB window; nothing may follow a sync for 0.83 ms.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

static void panel_cmd_before(void *f)
{
	ARG_UNUSED(f);
	panel_running();
}

ZTEST_SUITE(panel_cmd, NULL, NULL, panel_cmd_before, NULL, NULL);

static void expect_fill_window(int n, const char *hdr, uint8_t fill, size_t pixel_bytes)
{
	const struct g1_spim_entry *e = panel_xfer(n);
	uint8_t want[5];

	zassert_not_null(e, "no transfer %d", n);
	zassert_not_null(e->mosi);
	zassert_equal(g1_hex_parse(hdr, want, sizeof(want)), 5);
	zassert_mem_equal(e->mosi, want, 5, "transfer %d header", n);
	zassert_equal(e->len, 5 + pixel_bytes + 1, "transfer %d: %zu bytes", n, e->len);
	for (size_t i = 0; i < pixel_bytes; i++) {
		zassert_equal(e->mosi[5 + i], fill, "transfer %d pixel byte %zu", n, i);
	}
}

static void fill_frame_pattern(void)
{
	uint8_t *f = panel_frame();

	for (int i = 0; i < FRAME_BYTES; i++) {
		f[i] = (uint8_t)(i * 7 + i / 320);
	}
}

ZTEST(panel_cmd, test_send_cmd_is_one_byte_in_its_own_window)
{
	send_cmd(SPI_SYNC);

	zassert_equal(panel_xfer_count(), 1);
	expect_xfer(0, "97");
	zassert_equal(panel_xfer(0)->tx_len, 1);
}

ZTEST(panel_cmd, test_wr_status_reg_sends_register_then_value)
{
	wr_status_reg(SPI_WR_STATUS_REG2, 0x04);

	zassert_equal(panel_xfer_count(), 1);
	expect_xfer(0, "31 04");
}

ZTEST(panel_cmd, test_wr_cur_reg_sends_the_current_byte)
{
	wr_cur_reg(0x24);

	zassert_equal(panel_xfer_count(), 1);
	expect_xfer(0, "46 24");
}

ZTEST(panel_cmd, test_wr_lum_reg_sends_luminance_msb_first)
{
	wr_lum_reg(0x1bc0);

	zassert_equal(panel_xfer_count(), 1);
	expect_xfer(0, "36 1b c0");
}

ZTEST(panel_cmd, test_wr_offset_reg_sends_row_then_column_then_syncs)
{
	/* (0x0c, 0x0a) is the reset value */
	wr_offset_reg(0x0c, 0x0a);

	zassert_equal(panel_xfer_count(), 2);
	expect_xfer(0, "c0 0c 0a");
	expect_xfer(1, "97");
}

ZTEST(panel_cmd, test_nothing_follows_a_sync_within_the_reload_time)
{
	const struct g1_spim_entry *sync, *next;

	wr_offset_reg(0, 0);
	wr_offset_reg(24, 20);

	sync = panel_xfer(1);
	next = panel_xfer(2);
	zassert_not_null(next);
	zassert_equal(sync->head[0], SPI_SYNC);
	zassert_true(next->t_us - sync->t_us >= 830, "%u us after the sync",
		     next->t_us - sync->t_us);
}

ZTEST(panel_cmd, test_read_id_clocks_9f_and_three_more_bytes)
{
	panel_answer_id();

	zassert_equal(spi_read_id(), JBD_PANEL_DEVICE_ID);

	zassert_equal(panel_xfer_count(), 1);
	expect_xfer(0, "9f 00 00 00");
	zassert_equal(panel_xfer(0)->rx_len, 4);
}

ZTEST(panel_cmd, test_clr_cache_fills_the_frame_buffer)
{
	uint8_t *f = panel_frame();

	clr_cache(0x5a);

	for (int i = 0; i < FRAME_BYTES; i++) {
		zassert_equal(f[i], 0x5a, "frame byte %d", i);
	}
}

ZTEST(panel_cmd, test_clr_cache_writes_all_480_cache_rows_in_three_windows)
{
	clr_cache(0x5a);

	zassert_equal(panel_xfer_count(), 3);
	expect_fill_window(0, "02 00 00 00 ff", 0x5a, 192 * JBD_ROW_BYTES);
	expect_fill_window(1, "02 03 00 00 ff", 0x5a, 192 * JBD_ROW_BYTES);
	expect_fill_window(2, "02 06 00 00 ff", 0x5a, 96 * JBD_ROW_BYTES);
	zassert_equal(g1_fake_spim.too_long, 0, "a transfer exceeds the EasyDMA limit");
}

ZTEST(panel_cmd, test_jbd_clear_cache_sync_clears_then_syncs)
{
	jbd_clear_cache_sync(0);

	zassert_equal(panel_xfer_count(), 4);
	expect_fill_window(0, "02 00 00 00 ff", 0x00, 192 * JBD_ROW_BYTES);
	expect_fill_window(1, "02 03 00 00 ff", 0x00, 192 * JBD_ROW_BYTES);
	expect_fill_window(2, "02 06 00 00 ff", 0x00, 96 * JBD_ROW_BYTES);
	expect_xfer(3, "97");
}

static K_THREAD_STACK_DEFINE(clear_stack, 4096);
static struct k_thread clear_thread;

static void clear_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	jbd_clear_cache_sync(0);
}

ZTEST(panel_cmd, test_jbd_clear_cache_sync_waits_for_the_lcd_mutex)
{
	int while_held, joined;
	uint8_t frame_while_held;
	k_tid_t tid;

	memset(panel_frame(), 0xff, FRAME_BYTES);
	lock_lcd_mutex(K_FOREVER);
	tid = k_thread_create(&clear_thread, clear_stack, K_THREAD_STACK_SIZEOF(clear_stack),
			      clear_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_sleep(K_MSEC(50));
	while_held = panel_xfer_count();
	/* the fill precedes the locked transfer */
	frame_while_held = panel_frame()[0];
	unlock_lcd_mutex();
	joined = k_thread_join(tid, K_SECONDS(1));
	if (joined != 0) {
		k_thread_abort(tid);
	}

	zassert_equal(frame_while_held, 0x00, "the clearing thread did not run");
	zassert_equal(while_held, 0, "%d transfers while another thread held lcd_mutex",
		      while_held);
	zassert_equal(joined, 0, "jbd_clear_cache_sync() did not finish after the unlock");
	zassert_equal(panel_xfer_count(), 4);
}

ZTEST(panel_cmd, test_display_image_writes_one_window_at_x_y_then_syncs)
{
	const struct g1_spim_entry *e;

	fill_frame_pattern();
	display_image(0x20, 0x10, panel_frame(), 20 * JBD_ROW_BYTES);

	zassert_equal(panel_xfer_count(), 2);
	e = panel_xfer(0);
	zassert_mem_equal(e->mosi, ((uint8_t[]){0x02, 0x00, 0x40, 0x20, 0xff}), 5);
	zassert_equal(e->len, 5 + 20 * JBD_ROW_BYTES + 1);
	zassert_mem_equal(e->mosi + 5, panel_frame(), 20 * JBD_ROW_BYTES);
	expect_xfer(1, "97");
}

ZTEST(panel_cmd, test_full_frame_draw_splits_at_192_rows)
{
	struct jbd_panel_context *c = panel_ctx();
	const struct g1_spim_entry *e;

	/* current_row is x, current_column y */
	c->current_row = 0x20;
	c->current_column = 0x10;
	c->draw_image_cb(c);

	zassert_equal(panel_xfer_count(), 3);
	e = panel_xfer(0);
	zassert_mem_equal(e->mosi, ((uint8_t[]){0x02, 0x00, 0x40, 0x20, 0xff}), 5);
	zassert_equal(e->len, 5 + 192 * JBD_ROW_BYTES + 1);
	e = panel_xfer(1);
	zassert_mem_equal(e->mosi, ((uint8_t[]){0x02, 0x03, 0x40, 0x20, 0xff}), 5);
	zassert_equal(e->len, 5 + 8 * JBD_ROW_BYTES + 1);
	expect_xfer(2, "97");
	zassert_equal(g1_fake_spim.too_long, 0);
}

/*
 * Defect: spi_wr_buffer() ignores its buffer argument, so rows 0..7 are resent instead of 192..199.
 */
ZTEST_EXPECT_FAIL(panel_cmd, test_full_frame_draw_sends_rows_192_to_199_last);
ZTEST(panel_cmd, test_full_frame_draw_sends_rows_192_to_199_last)
{
	struct jbd_panel_context *c = panel_ctx();
	const struct g1_spim_entry *e;

	fill_frame_pattern();
	c->draw_image_cb(c);

	e = panel_xfer(1);
	zassert_not_null(e);
	zassert_equal(e->len, 5 + 8 * JBD_ROW_BYTES + 1);
	zassert_mem_equal(e->mosi + 5, panel_frame() + 192 * JBD_ROW_BYTES, 8 * JBD_ROW_BYTES,
			  "second window does not carry frame rows 192..199");
}

ZTEST(panel_cmd, test_draw_needs_an_initialised_panel)
{
	struct jbd_panel_context *c = panel_ctx();

	c->init_done = 0;
	c->draw_image_cb(c);

	zassert_equal(panel_xfer_count(), 0);
}
