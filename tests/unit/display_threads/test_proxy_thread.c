/*
 * Sync 6 subs: 7 power, 8 language, 9 startup mode, 10 dashboard toggle, 12 double tap, 13 long
 * press.
 */
#include "fakes.h"
#include "harness.h"

#include "display/proxy_thread.c"

extern struct k_msgq display_msgq;

FAKE_VALUE_FUNC(int, upgradeAppLanguageInfoToFlash, uint8_t);
FAKE_VALUE_FUNC(int, upgradeDashboardStartupModeInfoToFlash, uint8_t);
FAKE_VALUE_FUNC(int, upgradeDoubleTapCustomizeToFlash, uint32_t);
FAKE_VALUE_FUNC(int, upgradeLongpressTapCustomizeToFlash, uint32_t);
FAKE_VALUE_FUNC(atomic_val_t, setAudioBlockInUse, atomic_val_t);
FAKE_VALUE_FUNC(atomic_val_t, clearAudioBlockInUse);

static void proxy_before(void *fixture) {
  ARG_UNUSED(fixture);

  k_msgq_purge(&proxy_msgq);
  k_msgq_purge(&display_msgq);
  proxy_tid = NULL;
  memset(response_buf, 0, sizeof(response_buf));
  response_pending = 0;
  response_len = 0;
  audio_file_start = 0;
  audio_file_end = 0;
  audio_pkt_total = 0;
  audio_file_size = 0;
  audio_pkt_index = 0;
  audio_file_sent = 0;
  audio_block_idx = 0;
  audio_pkt_seq = 0;
  audio_sending = 0;
  RESET_FAKE(upgradeAppLanguageInfoToFlash);
  RESET_FAKE(upgradeDashboardStartupModeInfoToFlash);
  RESET_FAKE(upgradeDoubleTapCustomizeToFlash);
  RESET_FAKE(upgradeLongpressTapCustomizeToFlash);
  RESET_FAKE(setAudioBlockInUse);
  RESET_FAKE(clearAudioBlockInUse);
}

static void proxy_after(void *fixture) {
  ARG_UNUSED(fixture);

  if (proxy_tid) {
    k_thread_abort(proxy_tid);
    proxy_tid = NULL;
  }
  k_msgq_purge(&proxy_msgq);
  k_msgq_purge(&display_msgq);
}

ZTEST_SUITE(proxy_thread, NULL, NULL, proxy_before, proxy_after, NULL);

static struct GlassesState *proxy_start(void) {
  struct GlassesState *gs = g1_state();

  zassert_equal(start_proxy_thread_handler(gs), 0);
  g1_clock_advance_ms(1);
  return gs;
}

/* queues the bytes of hex as the response the next send_response_data_to_ble() writes */
static int load_response(const char *hex) {
  uint8_t data[PROXY_MSG_DATA_MAX];

  return send_response_data_to_msgqueue(data, g1_hex(hex, data, sizeof(data)));
}

/* sync_to_slave() call index sent sync command 6 with the bytes of hex */
static void expect_sync(int index, const char *hex) {
  expect_pkt(&g1_sync_to_slave_log, index, hex);
  zassert_equal_ptr(sync_to_slave_fake.arg0_history[index], g1_state());
  zassert_equal(sync_to_slave_fake.arg1_history[index], 6, "call %d: sync command %d", index,
                sync_to_slave_fake.arg1_history[index]);
}

/* starts the proxy thread; the master forwards value to the slave once, as sync 6 {sub, value} */
static void send_and_expect_sync(int (*send)(uint8_t value), uint8_t value, const char *sync) {
  proxy_start();
  zassert_equal(send(value), 0);
  g1_clock_advance_ms(1);
  zassert_equal(sync_to_slave_fake.call_count, 1);
  expect_sync(0, sync);
}

/* ---- response queue ---- */

ZTEST(proxy_thread, test_slave_queues_no_response) {
  g1_unit_set_role(G1_ROLE_SLAVE);

  zassert_equal(load_response("0a c9"), 0);
  zassert_equal(send_response_data_to_ble(), 0);
  zassert_equal(k_msgq_num_used_get(&proxy_msgq), 0);
}

ZTEST(proxy_thread, test_response_longer_than_20_bytes_is_rejected) {
  uint8_t resp[21] = {0};

  zassert_equal(send_response_data_to_msgqueue(resp, sizeof(resp)), -1);
  zassert_equal(send_response_data_to_msgqueue(resp, 20), 0);
  zassert_equal(k_msgq_num_used_get(&proxy_msgq), 1);
}

ZTEST(proxy_thread, test_loaded_response_is_written_to_the_phone_on_send) {
  proxy_start();
  load_response("0a 0a 00 05 c9 01 02 03 04 05");
  g1_clock_advance_ms(1);
  expect_no_frame();

  send_response_data_to_ble();
  g1_clock_advance_ms(1);

  expect_only_frame("0a 0a 00 05 c9 01 02 03 04 05");
}

ZTEST(proxy_thread, test_send_without_a_loaded_response_writes_nothing) {
  proxy_start();

  send_response_data_to_ble();
  g1_clock_advance_ms(1);

  expect_no_frame();
}

ZTEST(proxy_thread, test_a_loaded_response_is_written_once) {
  proxy_start();
  load_response("09 c9 07");
  send_response_data_to_ble();
  send_response_data_to_ble();
  g1_clock_advance_ms(1);

  expect_only_frame("09 c9 07");
}

ZTEST(proxy_thread, test_the_last_loaded_response_is_the_one_sent) {
  proxy_start();
  load_response("0a 01 02 03");
  load_response("0a c9");
  send_response_data_to_ble();
  g1_clock_advance_ms(1);

  expect_only_frame("0a c9");
}

ZTEST(proxy_thread, test_failed_write_drops_the_response) {
  proxy_start();
  g1_write_ret = -1;
  load_response("0a c9");
  send_response_data_to_ble();
  send_response_data_to_ble();
  g1_clock_advance_ms(1);

  expect_only_frame("0a c9");
}

ZTEST(proxy_thread, test_queue_holds_16_messages_and_rejects_the_17th) {
  for (int index = 0; index < 16; index++) {
    zassert_equal(send_response_data_to_ble(), 0, "message %d", index);
  }
  zassert_equal(send_response_data_to_ble(), -1);
  zassert_equal(load_response("00"), -1);
  zassert_equal(SendPowerInfoToSlave(1), -1);
  zassert_equal(SendSystemLanguageInfoToSlave(1), -1);
  zassert_equal(SendDashBoardStartupModeInfoToSlave(1), -1);
  zassert_equal(SlaveDoubleClickEventInject(), -1);
  zassert_equal(SendDoubleTapCustomizeToSlave(1), -1);
  zassert_equal(SendLongpressTapCustomizeToSlave(1), -1);
  zassert_equal(sendAudioStreamFileToApp(0, 1), -1);
  zassert_equal(requestAudioInfoToApp(0), -1);
  zassert_equal(k_msgq_num_used_get(&proxy_msgq), 16);
}

/* ---- master -> slave ---- */

ZTEST(proxy_thread, test_power_info_syncs_sub_7_and_raises_the_power_event_locally) {
  struct disp_msg msg;

  send_and_expect_sync(SendPowerInfoToSlave, 1, "07 01");
  zassert_equal(k_msgq_get(&display_msgq, &msg, K_NO_WAIT), 0);
  zassert_equal(msg.type, DISP_MSG_POWER_ON, "display message type %d", msg.type);
}

ZTEST(proxy_thread, test_power_info_0_raises_the_power_resume_event) {
  struct disp_msg msg;

  send_and_expect_sync(SendPowerInfoToSlave, 0, "07 00");
  zassert_equal(k_msgq_get(&display_msgq, &msg, K_NO_WAIT), 0);
  zassert_equal(msg.type, DISP_MSG_POWER_OFF, "display message type %d", msg.type);
}

/* sends the setting and expects one sync to the slave, then one store of the value */
#define STORED_SETTING_TEST(name, send, value, sync, store)                                                            \
  ZTEST(proxy_thread, test_##name) {                                                                                   \
    send_and_expect_sync(send, value, sync);                                                                           \
    zassert_equal(store##_fake.call_count, 1);                                                                         \
    zassert_equal(store##_fake.arg0_val, value);                                                                       \
  }

STORED_SETTING_TEST(language_syncs_sub_8_and_stores_it, SendSystemLanguageInfoToSlave, 3, "08 03",
                    upgradeAppLanguageInfoToFlash)
STORED_SETTING_TEST(startup_mode_syncs_sub_9_and_stores_it, SendDashBoardStartupModeInfoToSlave, 2, "09 02",
                    upgradeDashboardStartupModeInfoToFlash)
STORED_SETTING_TEST(double_tap_action_syncs_sub_12_and_stores_it, SendDoubleTapCustomizeToSlave, 4, "0c 04",
                    upgradeDoubleTapCustomizeToFlash)
STORED_SETTING_TEST(long_press_action_syncs_sub_13_and_stores_it, SendLongpressTapCustomizeToSlave, 1, "0d 01",
                    upgradeLongpressTapCustomizeToFlash)

ZTEST(proxy_thread, test_double_click_with_dashboard_action_opens_the_dashboard) {
  struct GlassesState *gs = proxy_start();

  gs->double_tap_customize = 4;
  gs->dashboard_mode = 0;
  zassert_equal(SlaveDoubleClickEventInject(), 0);
  g1_clock_advance_ms(1);

  zassert_equal(sync_to_slave_fake.call_count, 1);
  expect_sync(0, "0a 01");
  zassert_equal(gs->double_tap_dashboard_on, 1);
  zassert_true(atomic_get(&dashboard_flags) & BIT(1));
}

ZTEST(proxy_thread, test_double_click_on_the_open_dashboard_closes_it) {
  struct GlassesState *gs = proxy_start();

  gs->double_tap_customize = 4;
  gs->dashboard_mode = 6;
  gs->double_tap_dashboard_on = 1;
  gs->dashboard_ts->dashboard_hold = 1;
  SlaveDoubleClickEventInject();
  g1_clock_advance_ms(1);

  zassert_equal(sync_to_slave_fake.call_count, 1);
  expect_sync(0, "0a 02");
  zassert_equal(gs->double_tap_dashboard_on, 0);
  zassert_equal(gs->dashboard_ts->dashboard_hold, 0);
  zassert_true(atomic_get(&dashboard_flags) & BIT(1));
}

ZTEST(proxy_thread, test_double_click_with_another_action_syncs_nothing) {
  struct GlassesState *gs = proxy_start();

  gs->double_tap_customize = 1;
  gs->dashboard_mode = 0;
  SlaveDoubleClickEventInject();
  g1_clock_advance_ms(1);

  zassert_equal(sync_to_slave_fake.call_count, 0);
  zassert_equal(gs->double_tap_dashboard_on, 0);
  zassert_equal(atomic_get(&dashboard_flags), 0);
}

ZTEST(proxy_thread, test_sync_is_tried_three_times_and_nothing_stored_without_ack) {
  proxy_start();
  sync_to_slave_fake.return_val = 5000;

  SendDoubleTapCustomizeToSlave(4);
  g1_clock_advance_ms(1);

  zassert_equal(sync_to_slave_fake.call_count, 3);
  expect_sync(0, "0c 04");
  expect_sync(2, "0c 04");
  zassert_equal(upgradeDoubleTapCustomizeToFlash_fake.call_count, 0);
}

ZTEST(proxy_thread, test_sync_acknowledged_on_the_second_try_stores_once) {
  static int waits[] = {5000, 12};

  proxy_start();
  SET_RETURN_SEQ(sync_to_slave, waits, ARRAY_SIZE(waits));

  SendSystemLanguageInfoToSlave(5);
  g1_clock_advance_ms(1);

  zassert_equal(sync_to_slave_fake.call_count, 2);
  zassert_equal(upgradeAppLanguageInfoToFlash_fake.call_count, 1);
  zassert_equal(upgradeAppLanguageInfoToFlash_fake.arg0_val, 5);
}

ZTEST(proxy_thread, test_forwarding_waits_until_the_app_is_connected) {
  struct GlassesState *gs = g1_state();

  gs->module_mask &= ~BIT(7);
  proxy_start();
  SendSystemLanguageInfoToSlave(3);
  g1_clock_advance_ms(1500);

  zassert_equal(sync_to_slave_fake.call_count, 0);
  zassert_equal(k_msgq_num_used_get(&proxy_msgq), 1);

  gs->module_mask |= BIT(7);
  g1_clock_advance_ms(1100);
  zassert_equal(sync_to_slave_fake.call_count, 1);
}

ZTEST(proxy_thread, test_forwarding_waits_while_in_dfu_mode) {
  struct GlassesState *gs = g1_state();

  gs->work_mode = 1;
  proxy_start();
  SendSystemLanguageInfoToSlave(3);
  g1_clock_advance_ms(4000);

  zassert_equal(sync_to_slave_fake.call_count, 0);
  zassert_equal(k_msgq_num_used_get(&proxy_msgq), 1);
}

/* ---- QuickNote audio ---- */

#define VOICE_MGR_ADDR 0x400000
#define VOICE_BASE     0x421000

/* magic 5AA5AA5Ah, reserved, 4 x {state, start, end, timestamp, crc32} LE; state 2 = recorded */
static void put_manager(void) {
  memset(&g1_fake_flash_image[VOICE_MGR_ADDR], 0, 8 + 4 * 20);
  sys_put_le32(0x5aa5aa5a, &g1_fake_flash_image[VOICE_MGR_ADDR]);
}

static void put_recorded_block(int index, uint32_t start, uint32_t end, uint32_t ts, uint32_t crc) {
  const uint32_t fields[5] = {2, start, end, ts, crc};
  uint8_t *record = &g1_fake_flash_image[VOICE_MGR_ADDR + 8 + index * 20];

  for (int field = 0; field < 5; field++) {
    sys_put_le32(fields[field], &record[field * 4]);
  }
}

/* entries {block, timestamp LE32, crc32 LE32} */
ZTEST(proxy_thread, test_audio_info_lists_the_recorded_blocks) {
  put_manager();
  put_recorded_block(0, VOICE_BASE, VOICE_BASE + 300, 0x01020304, 0xaabbccdd);
  put_recorded_block(2, VOICE_BASE + 0x40000, VOICE_BASE + 0x40010, 0x11223344, 0x55667788);

  proxy_start();
  zassert_equal(requestAudioInfoToApp(0), 0);
  g1_clock_advance_ms(1);

  expect_only_frame("1e 18 00 00 01 02"
                    " 01 04 03 02 01 dd cc bb aa"
                    " 03 44 33 22 11 88 77 66 55");
}

/* chunk header: 1e len 00 seq 02 total LE16 index LE16 block, then the file bytes */
ZTEST(proxy_thread, test_audio_file_is_streamed_in_190_byte_chunks) {
  uint8_t want[200];

  put_manager();
  put_recorded_block(0, VOICE_BASE, VOICE_BASE + 300, 0x01020304, 0xaabbccdd);
  g1_fill_pattern(&g1_fake_flash_image[VOICE_BASE], 300, 0);

  proxy_start();
  zassert_equal(sendAudioStreamFileToApp(0, 1), 0);
  g1_clock_advance_ms(1);

  g1_hex("1e c8 00 00 02 02 00 01 00 01", want, 10);
  memcpy(&want[10], &g1_fake_flash_image[VOICE_BASE], 190);
  expect_frame_bytes(want, 200);
  g1_hex("1e 78 00 01 02 02 00 02 00 01", want, 10);
  memcpy(&want[10], &g1_fake_flash_image[VOICE_BASE + 190], 110);
  expect_only_frame_bytes(want, 120);

  zassert_true(clearAudioBlockInUse_fake.call_count >= 1);
}

/* setAudioBlockInUse() takes a 1-based block */
ZTEST(proxy_thread, test_sending_block_1_marks_block_1_in_use) {
  put_manager();
  put_recorded_block(0, VOICE_BASE, VOICE_BASE + 300, 0x01020304, 0xaabbccdd);

  proxy_start();
  sendAudioStreamFileToApp(0, 1);
  g1_clock_advance_ms(1);

  zassert_true(setAudioBlockInUse_fake.call_count >= 1);
  zassert_equal(setAudioBlockInUse_fake.arg0_history[0], 1, "marked %ld",
                (long)setAudioBlockInUse_fake.arg0_history[0]);
}
