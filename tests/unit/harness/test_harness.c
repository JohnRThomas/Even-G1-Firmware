#include "harness.h"
#include "fakes.h"

ZTEST_SUITE(harness, NULL, NULL, NULL, NULL, NULL);

static int write_frame(const char *hex)
{
  uint8_t buf[64];

  return g1_ble()->write(buf, g1_hex(hex, buf, sizeof(buf)));
}

ZTEST(harness, test_hex_parse_accepts_separators_0x_prefixes_and_comments)
{
  static const char text[] = "# 0x37 request, comment line\n"
           "37 4d,FB:0x0a\n"
           "c9ca  # trailing comment\n";
  uint8_t buf[8];

  zassert_equal(g1_hex_parse(text, buf, sizeof(buf)), 6);
  zassert_mem_equal(buf, "\x37\x4d\xfb\x0a\xc9\xca", 6);
  zassert_equal(g1_hex_parse("3", buf, sizeof(buf)), -EINVAL);
  zassert_equal(g1_hex_parse("zz", buf, sizeof(buf)), -EINVAL);
  zassert_equal(g1_hex_parse("01 02 03", buf, 2), -ENOSPC);
}

ZTEST(harness, test_frames_are_fifo_and_compared_with_length)
{
  write_frame("01 02");
  write_frame("03");
  zassert_equal(g1_frame_count(), 2);
  expect_frame("01 02");
  expect_only_frame_padded("03", 1);

  write_frame("37 37 00 00");
  expect_frame_padded("37 37", 4);
}

ZTEST(harness, test_frame_mismatch_message_shows_both_byte_strings)
{
  char msg[G1_MSG_SIZE];

  write_frame("37 37 05");
  zassert_false(g1_frame_check("37 37 06", -1, msg, sizeof(msg)));
  zassert_not_null(strstr(msg, "differs at byte 2"), "%s", msg);
  zassert_not_null(strstr(msg, "expected (3 bytes) 37 37 06"), "%s", msg);
  zassert_not_null(strstr(msg, "got      (3 bytes) 37 37 05"), "%s", msg);

  write_frame("37");
  zassert_false(g1_frame_check("37 00", -1, msg, sizeof(msg)));

  zassert_false(g1_frame_check("37", -1, msg, sizeof(msg)));
  zassert_not_null(strstr(msg, "no frame sent"), "%s", msg);

  write_frame("aa");
  zassert_false(g1_frame_check_none(msg, sizeof(msg)));
  zassert_not_null(strstr(msg, "aa"), "%s", msg);
  g1_frames_clear();
}

ZTEST(harness, test_write_returns_g1_write_ret_and_still_captures)
{
  g1_write_ret = -1;
  zassert_equal(write_frame("f1 00"), -1);
  expect_frame("f1 00");
}

ZTEST(harness, test_clock_starts_on_whole_second_and_advances_exactly)
{
  int64_t t0 = g1_clock_uptime_ms();

  zassert_equal(t0 % 1000, 0, "uptime %lld after reset", t0);
  g1_clock_advance_ms(30001);
  zassert_equal(g1_clock_uptime_ms() - t0, 30001);
  g1_clock_advance_ms(1);
  zassert_equal(g1_clock_uptime_ms() - t0, 30002);
}

/* tests run in name order */
ZTEST(harness, test_reset_1_dirty_everything)
{
  struct GlassesState *gs = g1_state();

  gs->is_master = 2;
  gs->dashboard_ts->ts = 1234;
  gs->ble_context.security_pending = 1;
  g1_clock_set_date(1);
  write_frame("01");
  set_date(99);
  g1_fake_flash_image[0x130000] = 0;
  g1_write_ret = -5;
  LOG_LEVEL = 5;
}

ZTEST(harness, test_reset_2_state_is_rebuilt_like_init_context)
{
  struct GlassesState *gs = g1_state();

  zassert_equal_ptr(gs, get_dashboard_state());
  zassert_equal_ptr(g1_ble(), &gs->ble_context);
  zassert_equal(gs->is_master, 1);
  zassert_equal(gs->dashboard_ts->ts, 0);
  zassert_equal(gs->ble_context.security_pending, 0);
  zassert_equal(gs->date_ts, G1_RESET_DATE);
  zassert_equal(gs->module_mask, 0x0dfb);
  expect_fill(gs->master_mac, sizeof(gs->master_mac), 0xff, "master_mac");
  expect_fill(gs->slave_mac, sizeof(gs->slave_mac), 0xff, "slave_mac");
  zassert_equal(gs->master_fw_ver.major, 1);
  zassert_equal(gs->master_fw_ver.minor, 6);
  zassert_equal(gs->master_fw_ver.patch, 6);
  zassert_equal(k_sem_count_get(&gs->ble_context.tx_sem), 0);
  expect_no_frame();
  zassert_equal(set_date_fake.call_count, 0);
  zassert_equal(g1_fake_flash_image[0x130000], 0xff);
  zassert_equal(g1_write_ret, 0);
  zassert_equal(LOG_LEVEL, 2);
  zassert_equal_ptr(gs->flash_read, g1_fake_flash_read);
}

ZTEST(harness, test_set_role_slave_like_role_init)
{
  struct GlassesState *gs = g1_state();

  g1_unit_set_role(G1_ROLE_SLAVE);
  zassert_equal(gs->is_master, 2);
  zassert_equal(gs->fw_ver.minor, 6);
  zassert_equal(gs->master_fw_ver.minor, 0);
  zassert_true(gs->esb_pkt.flags & BIT(2));
  zassert_false(gs->sync.flags & BIT(2));
}

ZTEST(harness, test_fake_flash_is_nor_and_survives_reboot_only)
{
  struct GlassesState *gs = g1_state();
  uint8_t byte = 0;

  /* NOR: writes only clear bits */
  zassert_ok(gs->flash_write(gs, 0x1000, "\x0f", 1));
  zassert_ok(gs->flash_write(gs, 0x1000, "\xf3", 1));
  zassert_ok(gs->flash_read(gs, 0x1000, &byte, 1));
  zassert_equal(byte, 0x03);
  zassert_equal(g1_fake_flash_write_fake.call_count, 2);
  zassert_equal(gs->flash_read(gs, G1_FAKE_FLASH_SIZE - 1, &byte, 2), -EINVAL);

  zassert_ok(flash_settings_write_and_verify(0x2000, "\x5a", 1));
  zassert_equal(g1_fake_flash_image[0x2000], 0x5a);
  zassert_equal(g1_fake_flash_image[0x2fff], 0x00);

  g1_unit_reboot();
  zassert_equal(g1_fake_flash_image[0x1000], 0x03);
  zassert_equal(g1_fake_flash_image[0x2000], 0x5a);
  zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);

  g1_unit_reset();
  zassert_equal(g1_fake_flash_image[0x1000], 0xff);
}

ZTEST(harness, test_unknown_vector_is_enoent)
{
  uint8_t buf[4];

  zassert_equal(g1_vector_load("no_such_vector", buf, sizeof(buf)), -ENOENT);
}

ZTEST(harness, test_vector_ends_at_the_next_title)
{
  uint8_t buf[16];
  int len;

  /* first and last packet of tests/unit/vectors/navigation.hex */
  len = g1_vector("0x0a_init", buf, sizeof(buf));
  expect_bytes(buf, len, "0a 06 00 6d 00 01");
  len = g1_vector("0x0a_exit", buf, sizeof(buf));
  expect_bytes(buf, len, "0a 06 00 b2 05 01");
}

ZTEST(harness, test_vector_tag_is_told_apart_by_its_command_byte)
{
  uint8_t buf[4];
  int len;

  /* "# exit" titles 0x18 in tests/unit/vectors/bitmap.hex and 0x0A in navigation.hex */
  len = g1_vector("0x18_exit", buf, sizeof(buf));
  expect_bytes(buf, len, "18");
  zassert_equal(g1_vector_load("0x0b_exit", buf, sizeof(buf)), -ENOENT);
}
