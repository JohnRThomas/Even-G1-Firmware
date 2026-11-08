/*
 * JBD013 command layer and frame buffer flush. Every command is its own CSB window; nothing may
 * follow a sync for 0.83 ms. A flush sends one 02 cache write per row, then a 97 sync; x_end is
 * exclusive, y_end inclusive.
 */
#include "panel_support.h"

/* the frame buffer with the 5 command header bytes in front of it and the 3 bytes behind it */
static uint8_t snapshot[5 + FRAME_BYTES + 3];

/* a running panel; frame rows 192..199 differ from rows 0..7 */
static void panel_before(void *fixture) {
  uint8_t *frame = panel_frame();

  ARG_UNUSED(fixture);
  panel_running();
  for (int index = 0; index < FRAME_BYTES; index++) {
    frame[index] = (uint8_t)(index * 7 + index / JBD_ROW_BYTES);
  }
}

ZTEST_SUITE(panel_cmd, NULL, NULL, panel_before, NULL, NULL);
ZTEST_SUITE(panel_flush, NULL, NULL, panel_before, NULL, NULL);

/*
 * Transfer index is a cache write of pixel_bytes frame bytes from frame_offset. The length is
 * checked with >=: the dummy byte of a flush row is checked by
 * row_window_ends_with_the_dummy_clocks.
 */
static void expect_frame_window(int index, const char *header, int frame_offset, size_t pixel_bytes) {
  const struct g1_spim_entry *entry = expect_window(index, header);

  zassert_true(entry->len >= 5 + pixel_bytes, "transfer %d: %zu bytes", index, entry->len);
  zassert_mem_equal(entry->mosi + 5, panel_frame() + frame_offset, pixel_bytes, "transfer %d pixels", index);
}

ZTEST(panel_cmd, test_send_cmd_is_one_byte_in_its_own_window) {
  send_cmd(SPI_SYNC);

  expect_xfers("97");
  zassert_equal(panel_xfer(0)->tx_len, 1);
}

/* the call sends exactly these transfers */
#define COMMAND_TEST(name, call, ...)                                                                                  \
  ZTEST(panel_cmd, test_##name) {                                                                                      \
    call;                                                                                                              \
    expect_xfers(__VA_ARGS__);                                                                                         \
  }

COMMAND_TEST(wr_status_reg_sends_register_then_value, wr_status_reg(SPI_WR_STATUS_REG2, 0x04), "31 04")
COMMAND_TEST(wr_cur_reg_sends_the_current_byte, wr_cur_reg(0x24), "46 24")
COMMAND_TEST(wr_lum_reg_sends_luminance_msb_first, wr_lum_reg(0x1bc0), "36 1b c0")
/* (0x0c, 0x0a) is the reset value */
COMMAND_TEST(wr_offset_reg_sends_row_then_column_then_syncs, wr_offset_reg(0x0c, 0x0a), "c0 0c 0a", "97")

ZTEST(panel_cmd, test_nothing_follows_a_sync_within_the_reload_time) {
  const struct g1_spim_entry *sync, *next;

  wr_offset_reg(0, 0);
  wr_offset_reg(24, 20);

  sync = panel_xfer(1);
  next = panel_xfer(2);
  zassert_not_null(next);
  zassert_equal(sync->mosi[0], SPI_SYNC);
  zassert_true(next->t_us - sync->t_us >= 830, "%u us after the sync", next->t_us - sync->t_us);
}

ZTEST(panel_cmd, test_read_id_clocks_9f_and_three_more_bytes) {
  panel_answer_id();

  zassert_equal(spi_read_id(), JBD_PANEL_DEVICE_ID);

  expect_xfers("9f 00 00 00");
  zassert_equal(panel_xfer(0)->rx_len, 4);
}

ZTEST(panel_cmd, test_clr_cache_fills_the_frame_buffer) {
  clr_cache(0x5a);

  expect_fill(panel_frame(), FRAME_BYTES, 0x5a, "frame");
}

ZTEST(panel_cmd, test_clr_cache_writes_all_480_cache_rows_in_three_windows) {
  clr_cache(0x5a);

  zassert_equal(g1_fake_spim.xfers, 3);
  expect_clear_windows(0, 0x5a);
  zassert_equal(g1_fake_spim.too_long, 0, "a transfer exceeds the EasyDMA limit");
}

ZTEST(panel_cmd, test_jbd_clear_cache_sync_clears_then_syncs) {
  jbd_clear_cache_sync(0);

  zassert_equal(g1_fake_spim.xfers, 4);
  expect_clear_windows(0, 0x00);
  expect_xfer(3, "97");
}

static void clear_entry(void *arg, void *unused1, void *unused2) {
  ARG_UNUSED(arg);
  ARG_UNUSED(unused1);
  ARG_UNUSED(unused2);
  jbd_clear_cache_sync(0);
}

ZTEST(panel_cmd, test_jbd_clear_cache_sync_waits_for_the_lcd_mutex) {
  int while_held, joined;
  uint8_t frame_while_held;
  k_tid_t tid;

  memset(panel_frame(), 0xff, FRAME_BYTES);
  lock_lcd_mutex(K_FOREVER);
  tid = g1_thread_start(clear_entry, NULL, K_PRIO_PREEMPT(5));
  k_sleep(K_MSEC(50));
  while_held = g1_fake_spim.xfers;
  /* the fill precedes the locked transfer */
  frame_while_held = panel_frame()[0];
  unlock_lcd_mutex();
  joined = k_thread_join(tid, K_SECONDS(1));

  zassert_equal(frame_while_held, 0x00, "the clearing thread did not run");
  zassert_equal(while_held, 0, "%d transfers while another thread held lcd_mutex", while_held);
  zassert_equal(joined, 0, "jbd_clear_cache_sync() did not finish after the unlock");
  zassert_equal(g1_fake_spim.xfers, 4);
}

ZTEST(panel_cmd, test_display_image_writes_one_window_at_x_y_then_syncs) {
  display_image(0x20, 0x10, panel_frame(), 20 * JBD_ROW_BYTES);

  zassert_equal(g1_fake_spim.xfers, 2);
  expect_frame_window(0, "02 00 40 20 ff", 0, 20 * JBD_ROW_BYTES);
  zassert_equal(panel_xfer(0)->len, 5 + 20 * JBD_ROW_BYTES + 1);
  expect_xfer(1, "97");
}

ZTEST(panel_cmd, test_full_frame_draw_splits_at_192_rows) {
  struct jbd_panel_context *panel = panel_ctx();
  const struct g1_spim_entry *entry;

  /* current_row is x, current_column y */
  panel->current_row = 0x20;
  panel->current_column = 0x10;
  panel->draw_image_cb(panel);

  zassert_equal(g1_fake_spim.xfers, 3);
  entry = expect_window(0, "02 00 40 20 ff");
  zassert_equal(entry->len, 5 + 192 * JBD_ROW_BYTES + 1);
  entry = expect_window(1, "02 03 40 20 ff");
  zassert_equal(entry->len, 5 + 8 * JBD_ROW_BYTES + 1);
  expect_xfer(2, "97");
  zassert_equal(g1_fake_spim.too_long, 0);
}

ZTEST(panel_cmd, test_full_frame_draw_sends_rows_192_to_199_last) {
  struct jbd_panel_context *panel = panel_ctx();
  const struct g1_spim_entry *entry;

  panel->draw_image_cb(panel);

  entry = panel_xfer(1);
  zassert_not_null(entry);
  zassert_equal(entry->len, 5 + 8 * JBD_ROW_BYTES + 1);
  zassert_mem_equal(entry->mosi + 5, panel_frame() + 192 * JBD_ROW_BYTES, 8 * JBD_ROW_BYTES,
                    "second window does not carry frame rows 192..199");
}

ZTEST(panel_cmd, test_draw_needs_an_initialised_panel) {
  struct jbd_panel_context *panel = panel_ctx();

  panel->init_done = 0;
  panel->draw_image_cb(panel);

  zassert_equal(g1_fake_spim.xfers, 0);
}

ZTEST(panel_flush, test_flush_is_refused_before_panel_init) {
  panel_ctx()->init_done = 0;

  zassert_equal(reflash_fb_data_to_lcd(0, 0, 0, 0, 640, 199), -1);

  zassert_equal(g1_fake_spim.xfers, 0);
}

ZTEST(panel_flush, test_flush_sends_one_window_per_row_then_syncs) {
  /* address (y << 10) | x */
  zassert_equal(reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 300, 12), 0);

  zassert_equal(g1_fake_spim.xfers, 4);
  expect_frame_window(0, "02 00 68 84 ff", 10 * JBD_ROW_BYTES + 50, 100);
  expect_frame_window(1, "02 00 6c 84 ff", 11 * JBD_ROW_BYTES + 50, 100);
  expect_frame_window(2, "02 00 70 84 ff", 12 * JBD_ROW_BYTES + 50, 100);
  expect_xfer(3, "97");
}

ZTEST(panel_flush, test_full_frame_flush_sends_200_rows_of_320_bytes) {
  zassert_equal(reflash_fb_data_to_lcd(0, 0, 0, 0, 640, 199), 0);

  zassert_equal(g1_fake_spim.xfers, FRAME_ROWS + 1);
  for (int row = 0; row < FRAME_ROWS; row++) {
    char header[32];
    uint32_t addr = (uint32_t)row << 10;

    snprintf(header, sizeof(header), "02 %02x %02x %02x ff", (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);
    expect_frame_window(row, header, row * JBD_ROW_BYTES, JBD_ROW_BYTES);
  }
  expect_frame_window(199, "02 03 1c 00 ff", 199 * JBD_ROW_BYTES, JBD_ROW_BYTES);
  expect_xfer(FRAME_ROWS, "97");
}

ZTEST(panel_flush, test_flush_leaves_the_frame_buffer_as_it_was) {
  uint8_t *start = panel_frame() - 5;

  memcpy(snapshot, start, sizeof(snapshot));

  zassert_equal(reflash_fb_data_to_lcd(0, 0, 0, 0, 640, 199), 0);
  zassert_equal(reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 301, 12), 0);

  zassert_mem_equal(start, snapshot, sizeof(snapshot));
}

ZTEST(panel_flush, test_row_window_ends_with_the_dummy_clocks) {
  zassert_equal(reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 300, 10), 0);

  zassert_equal(panel_xfer(0)->len, 5 + 100 + 1, "row window of %zu bytes for 100 pixel bytes", panel_xfer(0)->len);
}

ZTEST(panel_flush, test_flush_stops_at_the_last_frame_row) {
  zassert_equal(reflash_fb_data_to_lcd(0, 0, 0, 195, 20, 250), 0);

  zassert_equal(g1_fake_spim.xfers, 6, "%d transfers", g1_fake_spim.xfers);
}

ZTEST(panel_flush, test_flush_ex_reads_rows_shifted_by_offset) {
  zassert_equal(reflash_fb_data_to_lcd_ex(0x20, 0x10, 100, 10, 300, 11, 5), 0);

  zassert_equal(g1_fake_spim.xfers, 3);
  expect_frame_window(0, "02 00 68 84 ff", 15 * JBD_ROW_BYTES + 50, 100);
  expect_frame_window(1, "02 00 6c 84 ff", 16 * JBD_ROW_BYTES + 50, 100);
  expect_xfer(2, "97");
}

ZTEST(panel_flush, test_a_failed_row_does_not_stop_the_flush) {
  uint8_t *start = panel_frame() - 5;

  memcpy(snapshot, start, sizeof(snapshot));
  g1_fake_spim.xfer_error = NRFX_ERROR_BUSY;
  g1_fake_spim.xfer_error_skip = 1;
  g1_fake_spim.xfer_error_count = 1;

  (void)reflash_fb_data_to_lcd(0x20, 0x10, 100, 10, 300, 12);

  zassert_equal(g1_fake_spim.xfers, 4);
  zassert_equal(panel_xfer(1)->result, NRFX_ERROR_BUSY);
  expect_frame_window(2, "02 00 70 84 ff", 12 * JBD_ROW_BYTES + 50, 100);
  expect_xfer(3, "97");
  zassert_mem_equal(start, snapshot, sizeof(snapshot), "frame buffer not restored");
}
