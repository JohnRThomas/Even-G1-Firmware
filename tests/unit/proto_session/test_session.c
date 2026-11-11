/*
 * Link and session commands: 0x37 uptime, 0x4D MTU / session start, 0x2C device info, 0x33 / 0x34
 * serial numbers, 0x39 running app, 0x22 status, 0x3D language, 0x47 unpair, 0x17 erase DFU, 0x50
 * dashboard lock.
 */
#include <app_version.h>

#include "harness.h"
#include "fakes.h"
#include "ble/ble_proto.h"
#include "storage/storage.h"

ZTEST_SUITE(proto_session, NULL, NULL, NULL, NULL, NULL);

/*
 * Reply: 37 37 <u32 LE boot seconds> <session flag>; the flag needs tx_sem (given by 0x4D) and no
 * pending security.
 */
ZTEST(proto_session, test_0x37_uptime_replies_le32_seconds_and_flag_0_before_tx_ready)
{
  /* 3725 s = 0x00000e8d */
  g1_clock_advance_ms(3725 * 1000 + 999);

  g1_send_hex("37");

  expect_only_frame_padded("37 37 8d 0e 00 00 00", 20);
}

ZTEST(proto_session, test_0x37_uptime_flag_is_1_once_tx_ready_and_0_while_securing)
{
  /* the reset restarts the boot seconds */
  g1_clock_advance_ms(66 * 1000);
  k_sem_give(&g1_ble()->tx_sem);

  g1_send_hex("37");
  expect_frame_padded("37 37 42 00 00 00 01", 20);

  g1_ble()->security_pending = 1;
  g1_send_hex("37");
  expect_only_frame_padded("37 37 42 00 00 00 00", 20);
}

/* The MTU byte is unused; the ATT MTU comes from the stack's exchange. */
ZTEST(proto_session, test_0x4d_accepts_any_mtu_value)
{
  static const uint8_t values[] = {0x00, 0x17, 0xfb, 0xff};

  for (size_t index = 0; index < ARRAY_SIZE(values); index++) {
    g1_send_vector_with("0x4d_mtu_251", 1, values[index]);
    expect_frame_msg("4d c9", 20, "mtu byte 0x%02x", values[index]);
  }
  expect_no_frame();
  zassert_equal(change_work_mode_fake.call_count, 0);
}

ZTEST(proto_session, test_0x4d_session_flag_shows_in_0x37)
{
  g1_send_hex("37");
  expect_frame_padded("37 37 00 00 00 00 00", 20);

  g1_send_vector("0x4d_mtu_251");
  expect_frame_padded("4d c9", 20);

  g1_send_hex("37");
  expect_frame_padded("37 37 00 00 00 00 01", 20);
}

/* sub 1 = Android, 2 = iOS; any other sub leaves the phone type. Every request gets one reply. */
ZTEST(proto_session, test_0x2c_sub_byte_sets_the_phone_type)
{
  static const struct {
    uint8_t sub;
    uint8_t ios_before;
    uint8_t ios_after;
  } rows[] = {
    {0x01, 1, 0}, {0x02, 0, 1}, {0x05, 1, 1}, {0x00, 0, 0},
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    g1_state()->phone_is_ios = rows[row].ios_before;

    g1_send_vector_with("0x2c_android", 1, rows[row].sub);

    zassert_equal(g1_state()->phone_is_ios, rows[row].ios_after, "sub 0x%02x",
            rows[row].sub);
    zassert_equal(g1_frame_count(), row + 1, "sub 0x%02x", rows[row].sub);
  }
}

/* Defect: data[1] is read without a length check, so a lone 2C takes a stale byte. */
ZTEST_EXPECT_FAIL(proto_session, test_0x2c_without_sub_byte_leaves_the_phone_type_unchanged);
ZTEST(proto_session, test_0x2c_without_sub_byte_leaves_the_phone_type_unchanged)
{
  g1_send_hex("3f 02"); /* leaves 02 at byte 1 of the rx buffer */
  g1_frames_clear();

  g1_send_hex("2c");

  zassert_equal(g1_state()->phone_is_ios, 0, "phone type taken from a stale byte");
}

/*
 * Reply: 2C 66, own battery, other battery, voltage, current, temperature, master version, slave
 * version. The slave version is 1.6.5 so the two fields differ.
 */
ZTEST(proto_session, test_0x2c_master_reply_carries_batteries_and_both_versions)
{
  struct GlassesState *state = g1_state();
  const uint8_t want[20] = {0x2c, 0x66, 0x50, 0x3c, 0xee, 0x84, 0x1a,
          APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL,
          0x01, 0x06, 0x05};

  state->battery_left = 0x50;
  state->battery_right = 0x3c;
  state->battery_voltage = 0xee;
  state->battery_current = 0x84;
  state->battery_temp = 0x1a;
  state->fw_ver = (struct fw_ver){1, 6, 5};

  g1_send_vector("0x2c_android");

  expect_only_frame_bytes(want, sizeof(want));
}

ZTEST(proto_session, test_0x2c_slave_reply_has_no_master_version_and_its_own_version)
{
  struct GlassesState *state = g1_state();
  const uint8_t want[20] = {0x2c, 0x66, 0x58, 0x00, 0xe2, 0x88, 0x1f, 0x00, 0x00, 0x00,
          APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL};

  g1_unit_set_role(G1_ROLE_SLAVE);
  state->battery_left = 0x58;
  state->battery_right = 0x00;
  state->battery_voltage = 0xe2;
  state->battery_current = 0x88;
  state->battery_temp = 0x1f;

  g1_send_vector("0x2c_android");

  expect_only_frame_bytes(want, sizeof(want));
}

/* Defect: the second battery reads 100 whenever the own battery is above 96. */
ZTEST_EXPECT_FAIL(proto_session, test_0x2c_other_battery_is_not_replaced_by_a_full_own_battery);
ZTEST(proto_session, test_0x2c_other_battery_is_not_replaced_by_a_full_own_battery)
{
  const uint8_t *frame;

  g1_state()->battery_left = 100;
  g1_state()->battery_right = 50;

  g1_send_vector("0x2c_android");

  frame = g1_frame_peek(0, NULL);
  zassert_not_null(frame);
  zassert_equal(frame[2], 0x64);
  zassert_equal(frame[3], 0x32, "other lens reported as 0x%02x", frame[3]);
}

/* factory settings record, loaded as at boot */
static void load_settings_with(const char *sn, const char *name)
{
  struct sys_settings record;

  memset(&record, 0, sizeof(record));
  record.valid = 1;
  strncpy(record.sn, sn, sizeof(record.sn) - 1);
  strncpy(record.name, name, sizeof(record.name) - 1);
  memcpy(&g1_fake_flash_image[SYS_SETTINGS_ADDR], &record, sizeof(record));
  zassert_equal(load_sys_setting(g1_state()), 0);
}

ZTEST(proto_session, test_0x34_reports_the_serial_number_from_the_settings)
{
  load_settings_with("S110LAAL290026", "G1R1EKT099");

  g1_send_vector("0x34_device_sn_get");

  expect_only_frame("34 34 53 31 31 30 4c 41 41 4c 32 39 30 30 32 36 00 00 00 00");
}

/* the lens serial number is the record's name field */
ZTEST(proto_session, test_0x33_reports_the_lens_serial_number_from_the_settings)
{
  load_settings_with("S110LAAL290026", "G1R1EKT099");

  g1_send_vector("0x33_glasses_sn_get");

  expect_only_frame_padded("33 33 47 31 52 31 45 4b 54 30 39 39", 20);
}

/* Reply: the 5-byte request, then 00 when idle, else the running screen (dashboard_mode). */
#define RUNNING_APP_TEST(name, idle, request, reply)                                               \
  ZTEST(proto_session, name)                                                                 \
  {                                                                                          \
    is_idle_fake.return_val = (idle);                                                  \
    g1_state()->dashboard_mode = 6;                                                    \
    g1_send_hex(request);                                                              \
    expect_only_frame(reply);                                                          \
  }

RUNNING_APP_TEST(test_0x39_running_app_echoes_request_plus_zero_when_idle, true, "39 05 00 02 01",
     "39 05 00 02 01 00")
RUNNING_APP_TEST(test_0x39_running_app_reports_the_running_screen, false, "39 05 00 4a 01",
     "39 05 00 4a 01 06")
/* length field 6, packet 5 */
RUNNING_APP_TEST(test_0x39_length_mismatch_replies_ff, true, "39 06 00 02 01", "39 06 00 02 01 ff")

/* Reply: echo, unread count, note index + 1, low-power flag. */
ZTEST(proto_session, test_0x22_status_reports_unread_count_note_index_and_low_power)
{
  g1_send_vector_with("0x22_status_get", 3, 0x13);
  expect_frame("22 05 00 13 01 00 01 00");

  g1_state()->unread_msg_num = 3;
  getNotesIndex_fake.return_val = 1;
  g1_state()->dashboard_ts->low_power = 1;
  g1_send_vector("0x22_status_get");
  expect_only_frame("22 05 00 0a 01 03 02 01");
}

/* The handler has no error path; any reply would be the status. */
ZTEST(proto_session, test_0x22_length_mismatch_sends_no_status)
{
  g1_send_hex("22 06 00 0a 01");

  expect_no_frame();
}

ZTEST(proto_session, test_0x3d_master_echoes_and_forwards_the_language_to_the_slave)
{
  g1_send_vector("0x3d_language_en");

  expect_only_frame("3d 06 00 1a 01 02");
  zassert_equal(SendSystemLanguageInfoToSlave_fake.call_count, 1);
  zassert_equal(SendSystemLanguageInfoToSlave_fake.arg0_val, 2);
  zassert_equal(CURRENT_LANGUAGE.magic, 2);
}

ZTEST(proto_session, test_0x3d_slave_echoes_without_forwarding)
{
  g1_unit_set_role(G1_ROLE_SLAVE);

  g1_send_vector("0x3d_language_en");

  expect_only_frame("3d 06 00 1a 01 02");
  zassert_equal(SendSystemLanguageInfoToSlave_fake.call_count, 0);
  zassert_equal(CURRENT_LANGUAGE.magic, 2);
}

ZTEST(proto_session, test_0x3d_unknown_language_id_is_echoed_and_the_ui_language_kept)
{
  uint8_t before = CURRENT_LANGUAGE.language;

  g1_send_vector_with("0x3d_language_en", 5, 0x63);

  expect_only_frame("3d 06 00 1a 01 63");
  zassert_equal(CURRENT_LANGUAGE.language, before);
}

ZTEST(proto_session, test_0x3d_length_mismatch_is_dropped)
{
  g1_send_hex("3d 07 00 1a 01 02");

  expect_no_frame();
  zassert_equal(SendSystemLanguageInfoToSlave_fake.call_count, 0);
  zassert_equal(CURRENT_LANGUAGE.magic, 0);
}

/* Defect, skipped (stack overflow): data_len bytes are copied into a 21-byte reply. */
ZTEST(proto_session, test_0x3d_long_packet_does_not_overflow_the_reply)
{
  uint8_t pkt[30] = {0x3d, 30, 0x00, 0x1a, 0x01, 0x02};

  ztest_test_skip();

  g1_send(pkt, sizeof(pkt));
  zassert_true(g1_frame_count() <= 1);
}

ZTEST(proto_session, test_0x47_replies_c9_and_removes_the_bond)
{
  g1_send_vector("0x47_unpair");

  expect_only_frame_padded("47 c9", 20);
  zassert_equal(bt_unpair_fake.call_count, 1);
  zassert_equal(bt_unpair_fake.arg0_val, 0);
  zassert_is_null(bt_unpair_fake.arg1_val);
  zassert_equal(bt_conn_disconnect_fake.call_count, 0);
}

/* 0x13 = remote user terminated */
ZTEST(proto_session, test_0x47_disconnects_the_phone_after_the_reply)
{
  get_current_conn_fake.return_val = G1_FAKE_CONN;

  g1_send_vector("0x47_unpair");

  expect_frame_padded("47 c9", 20);
  zassert_equal(bt_conn_disconnect_fake.call_count, 1);
  zassert_equal_ptr(bt_conn_disconnect_fake.arg0_val, G1_FAKE_CONN);
  zassert_equal(bt_conn_disconnect_fake.arg1_val, 0x13);
}

ZTEST(proto_session, test_0x17_erases_the_dfu_slot_and_replies_c9)
{
  int64_t now;

  g1_clock_advance_ms(1234);
  now = g1_clock_uptime_ms();

  g1_send_vector("0x17_upgrade_mode");

  expect_only_frame_padded("17 c9", 20);
  zassert_equal(setEraseDFU_fake.call_count, 1);
  zassert_true(setEraseDFU_fake.arg0_val);
  zassert_equal(g1_state()->dashboard_cmd, 0x17);
  zassert_true(g1_dashboard_notified());
  zassert_equal(g1_state()->last_sync_ts, now);
}

/* The echo is sent later, when the lock is applied, outside this handler. */
ZTEST(proto_session, test_0x50_master_requests_the_lock_for_one_second)
{
  atomic_set_bit(&dashboard_lock_reported, DASHBOARD_LOCK_BIT);
  atomic_set_bit(&dashboard_head_up_opened, DASHBOARD_LOCK_BIT);

  g1_send_vector("0x50_dashboard_lock");

  expect_no_frame();
  zassert_true(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
  zassert_false(atomic_test_bit(&dashboard_lock_reported, DASHBOARD_LOCK_BIT));

  g1_clock_advance_ms(999);
  zassert_true(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
  g1_clock_advance_ms(2);
  zassert_false(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
  zassert_false(atomic_test_bit(&dashboard_head_up_opened, DASHBOARD_LOCK_BIT));
}

ZTEST(proto_session, test_0x50_repeat_while_locked_does_not_extend_the_lock)
{
  g1_send_vector("0x50_dashboard_lock");
  g1_clock_advance_ms(500);
  atomic_set_bit(&dashboard_lock_reported, DASHBOARD_LOCK_BIT);
  g1_send_vector("0x50_dashboard_lock");

  zassert_true(atomic_test_bit(&dashboard_lock_reported, DASHBOARD_LOCK_BIT));
  g1_clock_advance_ms(501);
  zassert_false(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
}

/* 50 <len16> <seq> <action, 01 = lock> 01 that must not lock; the handler never replies */
#define NO_LOCK_TEST(name, role, request)                                                          \
  ZTEST(proto_session, name)                                                                 \
  {                                                                                          \
    g1_unit_set_role(role);                                                            \
    g1_send_hex(request);                                                              \
    expect_no_frame();                                                                 \
    zassert_false(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));     \
  }

NO_LOCK_TEST(test_0x50_slave_ignores_the_lock, G1_ROLE_SLAVE, "50 06 00 00 01 01")
NO_LOCK_TEST(test_0x50_other_action_does_not_lock, G1_ROLE_MASTER, "50 06 00 00 02 01")
/* length field 7, packet 6 */
NO_LOCK_TEST(test_0x50_length_mismatch_does_not_lock, G1_ROLE_MASTER, "50 07 00 00 01 01")
