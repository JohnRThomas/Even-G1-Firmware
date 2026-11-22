/* Reply: <cmd> 01 <2|3> <n> <payload> <sum8>. Request b2 2 = from the charging case. */
#include <setjmp.h>

#include "fakes.h"
#include "pt_test.h"

#include "production_test/production_test.c"
#include "production_test/pt_data.c"

/* nfc/eeprom_st25dv.c: a mailbox read gives mailbox_rx, the writes go to mailbox_tx */
FAKE_VALUE_FUNC(int, eeprom_st25dv_read_i2c_mb_mailbox_ram, const struct device *, uint8_t **, uint32_t *);
FAKE_VALUE_FUNC(int, eeprom_st25dv_write_i2c_mb_mailbox_ram, const struct device *, const void *, size_t);
FAKE_VALUE_FUNC(uint8_t, eeprom_st25dv_lock, const struct device *);
FAKE_VALUE_FUNC(uint8_t, eeprom_st25dv_unlock, const struct device *);
/* sensors_audio/charger_box.c and nfc/nfc_wlc.c */
FAKE_VALUE_FUNC(int, enable_charger);
FAKE_VOID_FUNC(set_box_status, uint8_t, uint8_t);
FAKE_VALUE_FUNC(uint8_t, is_box_lid_closed);
FAKE_VALUE_FUNC(uint32_t, nfc_get_field_mv);

static uint8_t mailbox_rx[256];
static uint32_t mailbox_rx_len;
static struct g1_pkt_log mailbox_tx;

static int mailbox_read(const struct device *dev, uint8_t **buf, uint32_t *len) {
  ARG_UNUSED(dev);
  memcpy(*buf, mailbox_rx, mailbox_rx_len);
  *len = mailbox_rx_len;
  return 0;
}

static int mailbox_write(const struct device *dev, const void *buf, size_t len) {
  ARG_UNUSED(dev);
  g1_pkt_log_add(&mailbox_tx, buf, len);
  return 0;
}

static jmp_buf reboot_jmp;
static bool reboot_expected;
static int reboot_count;
static int reboot_type;
static int64_t reboot_uptime_ms;

FUNC_NORETURN void sys_reboot(int type) {
  reboot_count++;
  reboot_type = type;
  reboot_uptime_ms = k_uptime_get();
  if (reboot_expected) {
    longjmp(reboot_jmp, 1);
  }
  zassert_unreachable("unexpected sys_reboot(%d)", type);
  CODE_UNREACHABLE;
}

/* only compared, never read */
static struct device nfc_dev = {.name = "g1_unit_st25dv"};

static void pt_before(void *fixture) {
  ARG_UNUSED(fixture);

  RESET_FAKE(eeprom_st25dv_read_i2c_mb_mailbox_ram);
  RESET_FAKE(eeprom_st25dv_write_i2c_mb_mailbox_ram);
  RESET_FAKE(eeprom_st25dv_lock);
  RESET_FAKE(eeprom_st25dv_unlock);
  RESET_FAKE(enable_charger);
  RESET_FAKE(set_box_status);
  RESET_FAKE(nfc_get_field_mv);
  eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.custom_fake = mailbox_read;
  eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.custom_fake = mailbox_write;
  mailbox_rx_len = 0;
  g1_pkt_log_clear(&mailbox_tx);
  reboot_expected = false;
  reboot_count = 0;
  G1_PT_STATICS_RESET();
  serial_rx_clear();
}

ZTEST_SUITE(production_test, NULL, NULL, pt_before, NULL, NULL);

#define CASE_BATTERY_QUERY "13 01 02 00"

/* the lens gets the request over NFC; returns process_pt_data() */
static int nfc_request(const char *hex) {
  setNFCDevice(&nfc_dev);
  mailbox_rx_len = g1_hex(hex, mailbox_rx, sizeof(mailbox_rx));
  return process_pt_data(NULL);
}

/* nfc_request() that has to end in sys_reboot(); returns when it does */
static void nfc_request_reboots(const char *hex) {
  reboot_expected = true;
  if (setjmp(reboot_jmp) == 0) {
    nfc_request(hex);
    reboot_expected = false;
    zassert_unreachable("returned without sys_reboot()");
  }
  reboot_expected = false;
}

static void battery_charging(void) {
  g1_state()->battery_left = 50;
  g1_state()->battery_current_ma = 100;
  g1_state()->battery_temp_x100 = 2500;
  nfc_get_field_mv_fake.return_val = 5000;
}

ZTEST(production_test, test_comm_write_rejects_bad_arguments) {
  uint8_t reply[5] = {0x24};

  zassert_equal(comm_write(PT_COMM_NFC, NULL, reply, sizeof(reply)), -1);
  zassert_equal(comm_write(PT_COMM_NFC, &nfc_dev, NULL, sizeof(reply)), -1);
  zassert_equal(comm_write(PT_COMM_NFC, &nfc_dev, reply, 0), -1);
  zassert_equal(comm_write(PT_COMM_NONE, &nfc_dev, reply, sizeof(reply)), -1);
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.call_count, 0);
  zassert_equal(g1_pt_uart_tx_fake.call_count, 0);
}

ZTEST(production_test, test_comm_write_nfc_puts_bytes_in_mailbox) {
  uint8_t reply[] = {0x24, 0x01, 0x03, 0x03, 0x01, 0x06, 0x06, 0x38};

  zassert_equal(comm_write(PT_COMM_NFC, &nfc_dev, reply, sizeof(reply)), 0);
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.arg0_val, &nfc_dev);
  expect_pkt(&mailbox_tx, 0, "24 01 03 03 01 06 06 38");
  zassert_equal(g1_pt_uart_tx_fake.call_count, 0);

  G1_FAKE_RETURN(eeprom_st25dv_write_i2c_mb_mailbox_ram, -3);
  zassert_equal(comm_write(PT_COMM_NFC, &nfc_dev, reply, sizeof(reply)), -3);
}

ZTEST(production_test, test_comm_write_uart_sends_bytes_on_lpuart) {
  uint8_t frame[] = {0x5a, 0xa5, 0xff, 0x01, 0x07, 0x06};

  zassert_equal(comm_write(PT_COMM_UART, &g1_pt_uart_dev, frame, sizeof(frame)), 0);
  zassert_equal(g1_pt_uart_tx_fake.arg0_val, &g1_pt_uart_dev);
  expect_pkt(&g1_pt_uart_tx_log, 0, "5a a5 ff 01 07 06");
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.call_count, 0);
}

ZTEST(production_test, test_comm_write_unknown_transport_sends_nothing) {
  uint8_t reply = 0x24;

  zassert_equal(comm_write((enum pt_comm_type)3, &nfc_dev, &reply, 1), 0);
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.call_count, 0);
  zassert_equal(g1_pt_uart_tx_fake.call_count, 0);
}

ZTEST(production_test, test_comm_read_rejects_bad_arguments) {
  uint8_t buf[256];
  uint8_t *dest = buf;
  uint32_t len;

  zassert_equal(comm_read(PT_COMM_NFC, NULL, &dest, &len), -1);
  zassert_equal(comm_read(PT_COMM_NFC, &nfc_dev, NULL, &len), -1);
  zassert_equal(comm_read(PT_COMM_NFC, &nfc_dev, &dest, NULL), -1);
  zassert_equal(comm_read(PT_COMM_NONE, &nfc_dev, &dest, &len), -1);
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.call_count, 0);
}

ZTEST(production_test, test_comm_read_nfc_takes_message_from_mailbox) {
  uint8_t buf[256];
  uint8_t *dest = buf;
  uint32_t len = 0;

  mailbox_rx_len = g1_hex(CASE_BATTERY_QUERY, mailbox_rx, sizeof(mailbox_rx));

  zassert_equal(comm_read(PT_COMM_NFC, &nfc_dev, &dest, &len), 0);
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.arg0_val, &nfc_dev);
  expect_bytes(buf, len, CASE_BATTERY_QUERY);
}

ZTEST(production_test, test_comm_read_uart_takes_payload_of_serial_frame) {
  uint8_t buf[256];
  uint8_t *dest = buf;
  uint32_t len = 0;

  pt_test_uart_receive("5a a5 7f 04 24 01 01 00 a8", 0);

  zassert_equal(comm_read(PT_COMM_UART, &g1_pt_uart_dev, &dest, &len), 0);
  expect_bytes(buf, len, "24 01 01 00");
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.call_count, 0);
}

ZTEST(production_test, test_transport_setters_reject_null) {
  zassert_equal(setNFCDevice(NULL), -1);
  zassert_equal(setUARTDevice(NULL), -1);
  zassert_equal(setNFCDevice(&nfc_dev), 0);
  zassert_equal(setUARTDevice(NULL), -1);
  zassert_equal(pt_comm.type, PT_COMM_NFC, "a rejected device does not switch transport");
}

ZTEST(production_test, test_sensor_mutex_locks_nfc_tag_when_set) {
  lock_sensor_mutex();
  unlock_sensor_mutex();
  zassert_equal(eeprom_st25dv_lock_fake.call_count, 0);
  zassert_equal(eeprom_st25dv_unlock_fake.call_count, 0);

  setNFCDevice(&nfc_dev);
  lock_sensor_mutex();
  unlock_sensor_mutex();
  zassert_equal(eeprom_st25dv_lock_fake.call_count, 1);
  zassert_equal(eeprom_st25dv_lock_fake.arg0_val, &nfc_dev);
  zassert_equal(eeprom_st25dv_unlock_fake.call_count, 1);
}

ZTEST(production_test, test_pt_without_transport_returns_minus_1) {
  zassert_equal(process_pt_data(NULL), -1);
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.call_count, 0);
  zassert_equal(pt_comm_busy_set_fake.call_count, 0);
}

ZTEST(production_test, test_pt_mailbox_read_error_returns_minus_2) {
  G1_FAKE_RETURN(eeprom_st25dv_read_i2c_mb_mailbox_ram, -3);

  zassert_equal(nfc_request(""), -2);
  zassert_equal(mailbox_tx.count, 0);
  zassert_equal(pt_comm_busy_set_fake.call_count, 1);
  zassert_equal(pt_comm_busy_clear_fake.call_count, 1);
}

/* 01 06 06 = VERSION */
ZTEST(production_test, test_pt_version_request_over_nfc) {
  zassert_equal(nfc_request("24 01 01 00"), 0);
  zassert_equal(mailbox_tx.count, 1);
  expect_pkt(&mailbox_tx, 0, "24 01 03 03 01 06 06 38");
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.arg0_val, &nfc_dev);
  zassert_equal(pt_comm_busy_set_fake.call_count, 1);
  zassert_equal(pt_comm_busy_clear_fake.call_count, 1);
}

ZTEST(production_test, test_pt_unknown_command_replies_status_2) {
  zassert_equal(nfc_request("77 01 01 00"), 0);
  expect_pkt(&mailbox_tx, 0, "77 01 03 01 02 7e");
}

ZTEST(production_test, test_pt_empty_request_is_not_answered) {
  zassert_equal(nfc_request(""), -3);
  zassert_equal(mailbox_tx.count, 0);
  zassert_equal(pt_comm_busy_clear_fake.call_count, 1);
}

ZTEST(production_test, test_pt_mailbox_write_error_returns_minus_4) {
  G1_FAKE_RETURN(eeprom_st25dv_write_i2c_mb_mailbox_ram, -3);

  zassert_equal(nfc_request("24 01 01 00"), -4);
  zassert_equal(pt_comm_busy_clear_fake.call_count, 1);
}

/* then the stray 0x5a */
ZTEST(production_test, test_pt_version_request_over_uart_reply_is_framed) {
  zassert_equal(setUARTDevice(&g1_pt_uart_dev), 0);
  pt_test_uart_receive("5a a5 7f 04 24 01 01 00 a8", 0);

  zassert_equal(process_pt_data(NULL), 0);
  zassert_equal(g1_pt_uart_tx_log.count, 2);
  expect_pkt(&g1_pt_uart_tx_log, 0, "5a a5 ff 07 24 01 03 03 01 06 06 3d");
  zassert_equal(g1_pt_uart_tx_log.data[1][0], 0x5a);
  zassert_equal(mailbox_tx.count, 0);
}

ZTEST(production_test, test_pt_refused_command_reply_names_the_command) {
  zassert_equal(nfc_request("32 01 01 01 01"), 0);
  zassert_equal(mailbox_tx.count, 1);
  zassert_true(mailbox_tx.len[0] >= 6, "reply is %d byte(s), first 0x%02x", mailbox_tx.len[0], mailbox_tx.data[0][0]);
  zassert_equal(mailbox_tx.data[0][0], 0x32);
  zassert_not_equal(mailbox_tx.data[0][4], 0);
}

ZTEST(production_test, test_pt_flash_info_reply_fits_response_buffer) {
  g1_state()->flash_read = NULL; /* no flash API on the host */
  zassert_equal(nfc_request("1e 01 01 00"), 0);
  zassert_equal(mailbox_tx.len[0], 0x52 + 1);
}

/* status, field mV (BE), SoC %, 1 = discharging, |mA| */
ZTEST(production_test, test_pt_case_battery_query_reports_charging_state) {
  battery_charging();

  zassert_equal(nfc_request(CASE_BATTERY_QUERY), 0);
  zassert_equal(mailbox_tx.count, 1);
  expect_pkt(&mailbox_tx, 0, "13 01 02 06 00 13 88 32 00 64 4d");
}

ZTEST(production_test, test_pt_case_battery_status_ok_enables_charger) {
  battery_charging();

  zassert_equal(nfc_request(CASE_BATTERY_QUERY), 0);
  zassert_equal(enable_charger_fake.call_count, 1);
}

ZTEST(production_test, test_pt_case_battery_charger_error_returns_minus_5) {
  battery_charging();
  enable_charger_fake.return_val = -EIO;

  zassert_equal(nfc_request(CASE_BATTERY_QUERY), -5);
  zassert_equal(mailbox_tx.count, 1);
}

ZTEST(production_test, test_pt_case_battery_over_temperature_does_not_enable_charger) {
  battery_charging();
  g1_state()->battery_temp_x100 = 6000;

  zassert_equal(nfc_request(CASE_BATTERY_QUERY), 0);
  expect_pkt(&mailbox_tx, 0, "13 01 02 06 02 13 88 32 00 64 4f");
  zassert_equal(enable_charger_fake.call_count, 0);
}

ZTEST(production_test, test_pt_case_battery_full_does_not_enable_charger) {
  battery_charging();
  g1_state()->battery_left = 100;

  zassert_equal(nfc_request(CASE_BATTERY_QUERY), 0);
  expect_pkt(&mailbox_tx, 0, "13 01 02 06 01 13 88 64 00 64 80");
  zassert_equal(enable_charger_fake.call_count, 0);
}

ZTEST(production_test, test_pt_case_battery_discharging_sets_direction_byte) {
  battery_charging();
  g1_state()->battery_current_ma = -100;

  zassert_equal(nfc_request(CASE_BATTERY_QUERY), 0);
  zassert_equal(mailbox_tx.data[0][8], 1);
  zassert_equal(mailbox_tx.data[0][9], 100);
}

/* Field above 5.5 V on four polls: byte 9 = 0x80, resent 1 s later cleared. */
static void four_high_field_polls(void) {
  battery_charging();
  nfc_get_field_mv_fake.return_val = 6000;
  g1_clock_advance_ms(10001);
  for (int poll = 0; poll < 4; poll++) {
    zassert_equal(nfc_request(CASE_BATTERY_QUERY), 0);
  }
}

ZTEST(production_test, test_pt_case_battery_flagged_reply_is_sent_again_after_1s) {
  int64_t start = g1_clock_uptime_ms();

  four_high_field_polls();

  zassert_equal(mailbox_tx.count, 5, "3 plain replies, then 2 for the fourth poll");
  zassert_equal(mailbox_tx.data[2][9], 0x64, "third poll is not flagged");
  expect_pkt(&mailbox_tx, 3, "13 01 02 06 00 17 70 32 00 80 55");
  /* the resend has byte 9 cleared; its sum byte is checked below */
  zassert_equal(mailbox_tx.len[4], 11);
  expect_bytes(mailbox_tx.data[4], 10, "13 01 02 06 00 17 70 32 00 00");
  expect_elapsed_ms(start, 10001 + 1000, INT64_MAX);
}

ZTEST(production_test, test_pt_case_battery_repeated_reply_has_valid_checksum) {
  four_high_field_polls();

  zassert_equal(mailbox_tx.count, 5);
  zassert_equal(mailbox_tx.data[4][10], g1_sum8(mailbox_tx.data[4], 10), "sum byte 0x%02x", mailbox_tx.data[4][10]);
}

/*
 * Over the lpuart the reply sits behind the frame header (5a a5 ff n), so the flag is byte 13 of
 * what is sent, and the resend goes out on the lpuart too: not to the tag the lens used before.
 */
ZTEST(production_test, test_pt_case_battery_flagged_reply_over_uart_is_sent_again_on_the_uart) {
  battery_charging();
  nfc_get_field_mv_fake.return_val = 6000;
  g1_clock_advance_ms(10001);
  setNFCDevice(&nfc_dev);
  zassert_equal(setUARTDevice(&g1_pt_uart_dev), 0);

  for (int poll = 0; poll < 4; poll++) {
    pt_test_uart_receive("5a a5 7f 04 13 01 02 00 98", 0);
    zassert_equal(process_pt_data(NULL), 0);
  }

  /* every frame is followed by the stray 0x5a */
  zassert_equal(g1_pt_uart_tx_log.count, 10, "%d writes: 3 plain replies, then 2 for the fourth poll",
                g1_pt_uart_tx_log.count);
  expect_pkt(&g1_pt_uart_tx_log, 6, "5a a5 ff 0a 13 01 02 06 00 17 70 32 00 80 5d");
  expect_pkt(&g1_pt_uart_tx_log, 8, "5a a5 ff 0a 13 01 02 06 00 17 70 32 00 00 dd");
  zassert_equal(mailbox_tx.count, 0, "%d write(s) to the NFC mailbox", mailbox_tx.count);
}

ZTEST(production_test, test_pt_long_trip_from_case_arms_low_power) {
  zassert_equal(nfc_request("06 01 02 00"), 0);
  expect_pkt(&mailbox_tx, 0, "06 01 02 01 00 0a");
  zassert_equal(can_enter_low_power, 1);
  zassert_equal(set_box_status_fake.arg0_val, BOX_LID_CLOSED);
  zassert_equal(set_box_status_fake.arg1_val, 1);
  zassert_equal(nfc_deadline_set_fake.arg0_val, 10000);
  zassert_equal(change_work_mode_fake.call_count, 0);
}

ZTEST(production_test, test_pt_long_trip_from_tool_enters_work_mode_7_after_10s) {
  int64_t start = g1_clock_uptime_ms();

  zassert_equal(nfc_request("06 01 01 00"), 0);
  expect_pkt(&mailbox_tx, 0, "06 01 03 01 00 0b");
  zassert_equal(change_work_mode_fake.call_count, 1);
  zassert_equal(change_work_mode_fake.arg0_val, 7);
  expect_elapsed_ms(start, 10000, INT64_MAX);
  zassert_equal(can_enter_low_power, 0);
}

ZTEST(production_test, test_pt_restart_replies_then_reboots) {
  int64_t start = g1_clock_uptime_ms();

  nfc_request_reboots("0b 01 01 00");

  expect_pkt(&mailbox_tx, 0, "0b 01 03 01 00 10");
  zassert_equal(reboot_count, 1);
  zassert_equal(reboot_type, SYS_REBOOT_COLD);
  zassert_true(reboot_uptime_ms - start >= 300);
}

ZTEST(production_test, test_pt_enter_testmode_saves_and_reboots) {
  nfc_request_reboots("01 01 01 01 01");

  expect_pkt(&mailbox_tx, 0, "01 01 03 01 00 06");
  zassert_equal(get_pt_test_mode(), 1);
  zassert_equal(save_usr_settings_fake.call_count, 1);
  zassert_equal(reboot_type, SYS_REBOOT_COLD);
}

ZTEST(production_test, test_pt_enter_testmode_already_set_does_not_reboot) {
  pt_test_mode = 1;

  zassert_equal(nfc_request("01 01 01 01 01"), 0);
  expect_pkt(&mailbox_tx, 0, "01 01 03 01 04 0a");
  zassert_equal(save_usr_settings_fake.call_count, 0);
  zassert_equal(reboot_count, 0);
}

ZTEST(production_test, test_pt_enter_testmode_invalid_value_is_refused) {
  zassert_equal(nfc_request("01 01 01 01 02"), 0);
  expect_pkt(&mailbox_tx, 0, "01 01 03 01 03 09");
  zassert_equal(get_pt_test_mode(), 0);
  zassert_equal(reboot_count, 0);
}
