/* storage/flash_settings.c: reads with retry, write-and-verify, whitelist, fonts, CRC, settings. */
#include "harness.h"
#include "drivers.h"

#include "storage/flash_settings.c"

#define SECTOR    G1_NOR_SECTOR
#define USR_ADDR  0x134000
#define WL_ADDR   0x131000
#define FONT_ADDR 0x300000

/* what flash_settings.c uses from watchdog.c, threads.c and whitelist.c */
FAKE_VALUE_FUNC(int, watchdog_feed);
uint8_t watchdog_enabled;
uint8_t fuel_gauge_stuck;
static uint8_t whitelist[0x15e2];

uint8_t *get_app_whitelist_info(void)
{
  return whitelist;
}

static const uint8_t record[4] = { 0x01, 0x02, 0x03, 0x04 };
static const uint8_t record5[5] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
static const uint8_t glyph[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

static struct local_store first_store;

static void *flash_settings_setup(void)
{
  sett_init(&first_store);
  return NULL;
}

static void flash_settings_before(void *fixture)
{
  ARG_UNUSED(fixture);
  RESET_FAKE(watchdog_feed);
  watchdog_enabled = 0;
  fuel_gauge_stuck = 0;
}

ZTEST_SUITE(flash_settings, NULL, flash_settings_setup, flash_settings_before, NULL, NULL);

static const struct g1_nor_stats *st(void)
{
  return g1_nor_stats();
}

static void fill(uint32_t addr, uint8_t value, size_t len)
{
  memset(g1_nor_mem(addr, len), value, len);
}

/* every byte of the flash from addr to addr + len is value */
#define expect_flash_fill(addr, len, value, what)                                                  \
  expect_fill(g1_nor_mem(addr, len), len, value, what)

ZTEST(flash_settings, test_read_returns_the_bytes_at_the_address)
{
  uint8_t buf[5] = { 0 };

  memcpy(g1_nor_mem(USR_ADDR, 5), record5, 5);

  zassert_equal(flash_settings_read(USR_ADDR, buf, 5), 0);
  zassert_mem_equal(buf, record5, 5);
  zassert_equal(st()->calls[G1_NOR_READ], 1);
}

/* A 100 ms sleep on the 1 ms tick lasts up to 101 ms. */
ZTEST(flash_settings, test_read_retries_a_failed_read_every_100_ms)
{
  uint8_t buf[4] = { 0 };
  int64_t start = g1_clock_uptime_ms();

  memcpy(g1_nor_mem(USR_ADDR, 4), record, 4);
  g1_nor_fail(G1_NOR_READ, 2, -EIO);

  zassert_equal(flash_settings_read(USR_ADDR, buf, 4), 0);

  zassert_mem_equal(buf, record, 4);
  zassert_equal(st()->calls[G1_NOR_READ], 3);
  expect_elapsed_ms(start, 2 * 100, 2 * 101);
}

ZTEST(flash_settings, test_read_gives_up_after_ten_retries_with_the_error)
{
  uint8_t buf[4];
  int64_t start = g1_clock_uptime_ms();

  g1_nor_fail(G1_NOR_READ, 100, -EIO);

  zassert_equal(flash_settings_read(USR_ADDR, buf, 4), -EIO);
  zassert_equal(st()->calls[G1_NOR_READ], 11);
  expect_elapsed_ms(start, 10 * 100, 10 * 101);
}

ZTEST(flash_settings, test_read_without_the_flash_device_returns_minus_one)
{
  uint8_t buf[4];
  int64_t start = g1_clock_uptime_ms();

  g1_device_set_ready(G1_NOR_DEV, false);

  zassert_equal(flash_settings_read(USR_ADDR, buf, 4), -1);
  zassert_equal(st()->calls[G1_NOR_READ], 0);
  expect_elapsed_ms(start, 10 * 100, 10 * 101);
}

ZTEST(flash_settings, test_write_and_verify_replaces_the_sector_with_data_and_zero_fill)
{
  fill(USR_ADDR, 0xaa, SECTOR);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record5, sizeof(record5)), 0);

  zassert_mem_equal(g1_nor_mem(USR_ADDR, 5), record5, 5);
  expect_flash_fill(USR_ADDR + 5, SECTOR - 5, 0x00, "zero fill");
  expect_flash_fill(USR_ADDR + SECTOR, 16, 0xff, "next sector");
  zassert_equal(st()->calls[G1_NOR_ERASE], 1);
  zassert_equal(st()->last_addr[G1_NOR_ERASE], USR_ADDR);
  zassert_equal(st()->last_len[G1_NOR_ERASE], SECTOR);
  zassert_equal(st()->calls[G1_NOR_WRITE], 1);
  zassert_equal(st()->last_len[G1_NOR_WRITE], SECTOR);
  zassert_equal(st()->calls[G1_NOR_READ], 2);
}

ZTEST(flash_settings, test_write_and_verify_leaves_unchanged_data_alone)
{
  fill(USR_ADDR, 0xaa, SECTOR);
  memcpy(g1_nor_mem(USR_ADDR, 5), record5, 5);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record5, sizeof(record5)), 0);

  zassert_equal(st()->calls[G1_NOR_ERASE], 0);
  zassert_equal(st()->calls[G1_NOR_WRITE], 0);
  expect_flash_fill(USR_ADDR + 5, SECTOR - 5, 0xaa, "rest of the sector");
}

ZTEST(flash_settings, test_write_and_verify_reports_a_verify_mismatch_as_1)
{
  g1_nor_fail(G1_NOR_WRITE, 1, 0); /* write-protected: data dropped, success reported */

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record, sizeof(record)), 1);
}

ZTEST(flash_settings, test_write_and_verify_returns_a_read_error_without_erasing)
{
  g1_nor_fail(G1_NOR_READ, 1, -EIO);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record, sizeof(record)), -EIO);
  zassert_equal(st()->calls[G1_NOR_ERASE], 0);
}

ZTEST(flash_settings, test_write_and_verify_returns_an_erase_error_without_writing)
{
  fill(USR_ADDR, 0xaa, SECTOR);
  g1_nor_fail(G1_NOR_ERASE, 1, -EIO);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record, sizeof(record)), -EIO);
  zassert_equal(st()->calls[G1_NOR_WRITE], 0);
  expect_flash_fill(USR_ADDR, SECTOR, 0xaa, "old record");
}

ZTEST(flash_settings, test_write_and_verify_returns_a_write_error)
{
  g1_nor_fail(G1_NOR_WRITE, 1, -EIO);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record, sizeof(record)), -EIO);
}

ZTEST(flash_settings, test_write_and_verify_at_an_address_inside_a_sector_fails)
{
  fill(USR_ADDR, 0xaa, SECTOR);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR + 0x10, record, sizeof(record)),
          -EINVAL);
  expect_flash_fill(USR_ADDR, SECTOR, 0xaa, "sector");
}

ZTEST(flash_settings, test_write_and_verify_without_the_flash_device_returns_minus_one)
{
  g1_device_set_ready(G1_NOR_DEV, false);

  zassert_equal(flash_settings_write_and_verify(USR_ADDR, record, sizeof(record)), -1);
  zassert_equal(st()->calls[G1_NOR_READ] + st()->calls[G1_NOR_ERASE] +
            st()->calls[G1_NOR_WRITE], 0);
}

/* DEFECT: no length check; overruns the 4 KiB heap buffer. Skipped. */
ZTEST(flash_settings, test_write_and_verify_refuses_more_than_one_sector)
{
  static uint8_t big[SECTOR + 16];

  ztest_test_skip();
  memset(big, 0x5a, sizeof(big));
  zassert_not_equal(flash_settings_write_and_verify(USR_ADDR, big, sizeof(big)), 0);
  zassert_equal(st()->calls[G1_NOR_WRITE], 0);
}

ZTEST(flash_settings, test_whitelist_is_written_over_two_sectors)
{
  g1_fill_pattern(whitelist, sizeof(whitelist), 7);

  zassert_equal(write_whitelist_to_flash(WL_ADDR, whitelist, sizeof(whitelist)), 0);

  zassert_mem_equal(g1_nor_mem(WL_ADDR, sizeof(whitelist)), whitelist, sizeof(whitelist));
  expect_flash_fill(WL_ADDR + sizeof(whitelist), 2 * SECTOR - sizeof(whitelist), 0x00,
        "zero fill");
  expect_flash_fill(WL_ADDR + 2 * SECTOR, 16, 0xff, "third sector");
  zassert_equal(st()->calls[G1_NOR_ERASE], 2);
}

ZTEST(flash_settings, test_whitelist_with_a_failing_page_reports_the_error)
{
  memset(whitelist, 0x3c, sizeof(whitelist));
  g1_nor_fail(G1_NOR_ERASE, 1, -EIO);

  zassert_equal(write_whitelist_to_flash(WL_ADDR, whitelist, sizeof(whitelist)), -EIO);
  expect_flash_fill(WL_ADDR + SECTOR, sizeof(whitelist) - SECTOR, 0x3c, "second page");
}

ZTEST(flash_settings, test_whitelist_of_exactly_one_sector_writes_one_sector)
{
  memset(whitelist, 0x3c, SECTOR);
  fill(WL_ADDR + SECTOR, 0x00, 16);

  zassert_equal(write_whitelist_to_flash(WL_ADDR, whitelist, SECTOR), 0);

  expect_flash_fill(WL_ADDR, SECTOR, 0x3c, "first sector");
  expect_flash_fill(WL_ADDR + SECTOR, 16, 0x00, "next sector");
  zassert_equal(st()->calls[G1_NOR_ERASE], 1);
}

ZTEST(flash_settings, test_font_write_programs_len_bytes_and_leaves_the_rest_erased)
{
  fill(FONT_ADDR, 0x00, SECTOR);

  zassert_equal(write_font(FONT_ADDR, glyph, sizeof(glyph)), 0);

  zassert_mem_equal(g1_nor_mem(FONT_ADDR, 12), glyph, 12);
  expect_flash_fill(FONT_ADDR + 12, SECTOR - 12, 0xff, "rest of the sector");
  zassert_equal(st()->calls[G1_NOR_WRITE], 1);
  zassert_equal(st()->last_len[G1_NOR_WRITE], 12);
}

ZTEST(flash_settings, test_font_write_leaves_unchanged_data_alone)
{
  memcpy(g1_nor_mem(FONT_ADDR, 12), glyph, 12);

  zassert_equal(write_font(FONT_ADDR, glyph, sizeof(glyph)), 0);
  zassert_equal(st()->calls[G1_NOR_ERASE], 0);
  zassert_equal(st()->calls[G1_NOR_WRITE], 0);
}

ZTEST(flash_settings, test_font_write_retries_after_a_failed_verify)
{
  int64_t start = g1_clock_uptime_ms();

  g1_nor_fail(G1_NOR_WRITE, 1, 0);

  zassert_equal(write_font(FONT_ADDR, glyph, 8), 0);

  zassert_mem_equal(g1_nor_mem(FONT_ADDR, 8), glyph, 8);
  zassert_equal(st()->calls[G1_NOR_WRITE], 2);
  expect_elapsed_ms(start, 100, 101);
}

ZTEST(flash_settings, test_font_write_gives_up_after_ten_attempts)
{
  int64_t start = g1_clock_uptime_ms();

  g1_nor_fail(G1_NOR_WRITE, 100, 0);

  zassert_equal(write_font(FONT_ADDR, glyph, 8), 1);
  zassert_equal(st()->calls[G1_NOR_WRITE], 10);
  expect_elapsed_ms(start, 9 * 100, 9 * 101);
}

ZTEST(flash_settings, test_font_write_without_the_flash_device_gives_up_with_minus_one)
{
  int64_t start = g1_clock_uptime_ms();

  g1_device_set_ready(G1_NOR_DEV, false);

  zassert_equal(write_font(FONT_ADDR, glyph, 8), -1);
  expect_elapsed_ms(start, 9 * 100, 9 * 101);
}

/* DEFECT: the QSPI driver rejects lengths > 4 not a multiple of 4; the tail is never stored. */
ZTEST_EXPECT_FAIL(flash_settings, test_font_write_stores_a_length_that_is_not_a_multiple_of_4);
ZTEST(flash_settings, test_font_write_stores_a_length_that_is_not_a_multiple_of_4)
{
  static const uint8_t tail[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

  zassert_equal(write_font(FONT_ADDR, tail, sizeof(tail)), 0);
  zassert_mem_equal(g1_nor_mem(FONT_ADDR, sizeof(tail)), tail, sizeof(tail));
}

ZTEST(flash_settings, test_crc_of_the_check_string_is_0xb4c8)
{
  uint16_t crc = 0xffff;

  memcpy(g1_nor_mem(0x140000, 9), "123456789", 9);

  zassert_equal(calc_flash_crc(&crc, 0x140000, 9), 0);
  zassert_equal(crc, 0xb4c8, "crc 0x%04x", crc);
  zassert_equal(st()->calls[G1_NOR_READ], 1);
  zassert_equal(st()->last_len[G1_NOR_READ], 9);
}

ZTEST(flash_settings, test_crc_refuses_a_null_crc_or_an_empty_range)
{
  uint16_t crc = 0xffff;

  zassert_equal(calc_flash_crc(NULL, 0x140000, 9), -1);
  zassert_equal(calc_flash_crc(&crc, 0x140000, 0), -1);
  zassert_equal(crc, 0xffff);
  zassert_equal(st()->calls[G1_NOR_READ], 0);
}

ZTEST(flash_settings, test_crc_without_the_flash_device_returns_minus_one)
{
  uint16_t crc = 0xffff;

  g1_device_set_ready(G1_NOR_DEV, false);

  zassert_equal(calc_flash_crc(&crc, 0x140000, 9), -1);
  zassert_equal(crc, 0xffff);
}

ZTEST(flash_settings, test_crc_read_error_returns_minus_one)
{
  uint16_t crc = 0xffff;

  g1_nor_fail(G1_NOR_READ, 1, -EIO);

  zassert_equal(calc_flash_crc(&crc, 0x140000, 9), -1);
  zassert_equal(crc, 0xffff);
}

ZTEST(flash_settings, test_crc_feeds_the_watchdog_only_when_it_runs_and_the_gauge_is_ok)
{
  uint16_t crc = 0xffff;

  calc_flash_crc(&crc, 0x140000, 9);
  zassert_equal(watchdog_feed_fake.call_count, 0, "watchdog disabled");

  watchdog_enabled = 1;
  fuel_gauge_stuck = 1;
  calc_flash_crc(&crc, 0x140000, 9);
  zassert_equal(watchdog_feed_fake.call_count, 0, "gauge stuck");

  fuel_gauge_stuck = 0;
  calc_flash_crc(&crc, 0x140000, 9);
  zassert_equal(watchdog_feed_fake.call_count, 1);
}

/*
 * 0x1379 from an independent CRC-16/USB. DEFECT: a uint8_t loop index hangs for len >= 256.
 * Skipped.
 */
ZTEST(flash_settings, test_crc_over_300_bytes)
{
  uint16_t crc = 0xffff;
  uint8_t *mem = g1_nor_mem(0x140000, 300);

  ztest_test_skip();
  for (int offset = 0; offset < 300; offset++) {
    mem[offset] = (uint8_t)(offset * 13 + 5);
  }
  zassert_equal(calc_flash_crc(&crc, 0x140000, 300), 0);
  zassert_equal(crc, 0x1379, "crc 0x%04x", crc);
}

ZTEST(flash_settings, test_sett_init_mounts_the_settings_and_installs_the_local_store)
{
  zassert_equal(first_store.status, 1, "status %d", first_store.status);
  zassert_equal_ptr(first_store.write, local_store_write);
  zassert_equal_ptr(first_store.load, local_store_load);
  zassert_equal_ptr(first_store.del, local_store_delete);
}

struct read_req {
  void *dst;
  size_t len;
  int calls;
  int ret;
};

static ssize_t record_read(void *cb_arg, void *data, size_t len)
{
  struct read_req *req = cb_arg;

  req->dst = data;
  req->len = len;
  req->calls++;
  if (req->ret < 0) {
    return req->ret;
  }
  memset(data, 0x5a, len);
  return len;
}

ZTEST(flash_settings, test_sett_handler_loads_each_key_into_its_field)
{
  struct GlassesState *gs = g1_state();
  const struct {
    const char *key;
    void *dst;
    size_t len;
  } keys[] = {
    { "ble_whitelist_app", whitelist, 0x15e2 },
    { "dashboard_ts", gs->dashboard_ts, 0x75 },
    { "countdown_ts", gs->countdown_ts, 7 },
    { "first_enter_flag", &gs->first_enter_flag, 4 },
    { "brightness_level", &gs->panel.panel_brightness_level, 1 },
    { "3dof_enable", &gs->imu.dof3_enable, 1 },
    { "display_mode", &gs->display_mode, 1 },
    { "runtime_mode", &gs->work_mode, 1 },
    { "user_sched_info", gs->user_sched_info, 0x16a },
    { "wakeup_angle", &gs->imu.level, 1 },
  };

  for (size_t row = 0; row < ARRAY_SIZE(keys); row++) {
    struct read_req req = { 0 };

    zassert_equal(sett_handler(keys[row].key, keys[row].len, record_read, &req), 0,
            "%s", keys[row].key);
    zassert_equal(req.calls, 1, "%s", keys[row].key);
    zassert_equal_ptr(req.dst, keys[row].dst, "%s: wrong field", keys[row].key);
    zassert_equal(req.len, keys[row].len, "%s: length %u", keys[row].key,
            (unsigned int)req.len);
  }
}

ZTEST(flash_settings, test_sett_handler_returns_the_read_error)
{
  struct read_req req = { .ret = -EIO };

  zassert_equal(sett_handler("brightness_level", 1, record_read, &req), -EIO);
}

ZTEST(flash_settings, test_sett_handler_ignores_unknown_keys)
{
  struct read_req req = { 0 };

  zassert_equal(sett_handler("no_such_key", 1, record_read, &req), 0);
  zassert_equal(req.calls, 0);
}

ZTEST(flash_settings, test_sett_handler_skips_blocks_that_are_not_allocated)
{
  struct GlassesState *gs = g1_state();
  void *dash = gs->dashboard_ts;
  void *count = gs->countdown_ts;
  void *sched = gs->user_sched_info;
  struct read_req req = { 0 };

  gs->dashboard_ts = NULL;
  gs->countdown_ts = NULL;
  gs->user_sched_info = NULL;
  zassert_equal(sett_handler("dashboard_ts", 0x75, record_read, &req), 0);
  zassert_equal(sett_handler("countdown_ts", 7, record_read, &req), 0);
  zassert_equal(sett_handler("user_sched_info", 0x16a, record_read, &req), 0);
  gs->dashboard_ts = dash;
  gs->countdown_ts = count;
  gs->user_sched_info = sched;

  zassert_equal(req.calls, 0);
}

ZTEST(flash_settings, test_local_store_value_comes_back_on_load)
{
  uint8_t level = 7;

  zassert_equal(local_store_write("brightness_level", &level, 1), 0);
  g1_state()->panel.panel_brightness_level = 0;

  zassert_ok(settings_load_subtree("ar_glass_userdata_settings"));
  zassert_equal(g1_state()->panel.panel_brightness_level, 7);
}

ZTEST(flash_settings, test_local_store_delete_removes_the_key)
{
  uint8_t mode = 3;

  zassert_equal(local_store_write("display_mode", &mode, 1), 0);
  local_store_delete("display_mode");
  g1_state()->display_mode = 0x55;

  zassert_ok(settings_load_subtree("ar_glass_userdata_settings"));
  zassert_equal(g1_state()->display_mode, 0x55);
}

/* DEFECT: local_store_write() returns 0 when the save fails. */
ZTEST_EXPECT_FAIL(flash_settings, test_local_store_write_reports_a_failed_save);
ZTEST(flash_settings, test_local_store_write_reports_a_failed_save)
{
  static uint8_t big[SECTOR];

  zassert_not_equal(local_store_write("user_sched_info", big, sizeof(big)), 0);
}

/* DEFECT: unbounded strcat() into path[128] overflows the stack. Skipped. */
ZTEST(flash_settings, test_local_store_refuses_a_key_longer_than_the_path_buffer)
{
  static char key[160];
  uint8_t value = 1;

  ztest_test_skip();
  memset(key, 'k', sizeof(key) - 1);
  zassert_not_equal(local_store_write(key, &value, 1), 0);
}
