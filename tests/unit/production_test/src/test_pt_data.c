/* Reply: <cmd> 01 <2|3> <n> <payload> <sum8>. Request b2 2 = from the charging case. */
#include <string.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_misc.h"
#include "fakes_pt.h"
#include "pt_test.h"

#include "production_test/pt_data.c"
#include "production_test/production_test.c"

/* only compared, never read */
static const struct device nfc_dev = {.name = "g1_unit_st25dv"};

static void pt_before(void *fixture)
{
	ARG_UNUSED(fixture);

	pt_comm.nfc = NULL;
	pt_comm.uart = (const struct device *)1;
	pt_comm.type = PT_COMM_NFC;
	memset(pt_resp_buf, 0, sizeof(pt_resp_buf));
	memset(pt_req_buf, 0, sizeof(pt_req_buf));

	pt_test_mode = 0;
	pt_level_flags.raw = 0;
	aging_mode = 0;
	aging_phase = 0;
	battery_full_flag = false;
	battery_high_v_count = 0;
	battery_full_count = 0;
	battery_info_last_ms = 0;
	charger_enable_ok = true;
	memset(pt_uart_frame, 0, sizeof(pt_uart_frame));

	serial_rx_clear();
}

ZTEST_SUITE(production_test, NULL, NULL, pt_before, NULL, NULL);

static void mailbox_put(const uint8_t *req, size_t n)
{
	memcpy(g1_pt_mailbox_rx, req, n);
	g1_pt_mailbox_rx_len = n;
}

static void expect_mailbox_reply(int i, const uint8_t *exp, size_t n)
{
	char got[3 * G1_PKT_LOG_SIZE + 4];
	char want[3 * G1_PKT_LOG_SIZE + 4];

	zassert_true(g1_pt_mailbox_tx.count > i, "only %d mailbox writes", g1_pt_mailbox_tx.count);
	g1_hex_format(g1_pt_mailbox_tx.data[i], g1_pt_mailbox_tx.len[i], got, sizeof(got));
	g1_hex_format(exp, n, want, sizeof(want));
	zassert_true(g1_pt_mailbox_tx.len[i] == n &&
			     memcmp(g1_pt_mailbox_tx.data[i], exp, n) == 0,
		     "mailbox write %d\n  got  %s\n  want %s", i, got, want);
}

static void battery_charging(void)
{
	g1_state()->battery_left = 50;
	g1_state()->battery_current_ma = 100;
	g1_state()->battery_temp_x100 = 2500;
	nfc_get_field_mv_fake.return_val = 5000;
}

ZTEST(production_test, test_comm_write_rejects_bad_arguments)
{
	uint8_t b[5] = {0x24};

	zassert_equal(comm_write(PT_COMM_NFC, NULL, b, sizeof(b)), -1, NULL);
	zassert_equal(comm_write(PT_COMM_NFC, (void *)&nfc_dev, NULL, sizeof(b)), -1, NULL);
	zassert_equal(comm_write(PT_COMM_NFC, (void *)&nfc_dev, b, 0), -1, NULL);
	zassert_equal(comm_write(PT_COMM_NONE, (void *)&nfc_dev, b, sizeof(b)), -1, NULL);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.call_count, 0, NULL);
	zassert_equal(g1_pt_uart_tx_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_comm_write_nfc_puts_bytes_in_mailbox)
{
	static const uint8_t b[] = {0x24, 0x01, 0x03, 0x03, 0x01, 0x06, 0x06, 0x38};

	zassert_equal(comm_write(PT_COMM_NFC, (void *)&nfc_dev, (uint8_t *)b, sizeof(b)), 0, NULL);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.arg0_val, &nfc_dev, NULL);
	expect_mailbox_reply(0, b, sizeof(b));
	zassert_equal(g1_pt_uart_tx_fake.call_count, 0, NULL);

	eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.custom_fake = NULL;
	eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.return_val = -3;
	zassert_equal(comm_write(PT_COMM_NFC, (void *)&nfc_dev, (uint8_t *)b, sizeof(b)), -3, NULL);
}

ZTEST(production_test, test_comm_write_uart_sends_bytes_on_lpuart)
{
	static const uint8_t b[] = {0x5a, 0xa5, 0xff, 0x01, 0x07, 0x06};

	zassert_equal(comm_write(PT_COMM_UART, (void *)&g1_pt_uart_dev, (uint8_t *)b, sizeof(b)), 0,
		      NULL);
	zassert_equal(g1_pt_uart_tx_fake.arg0_val, &g1_pt_uart_dev, NULL);
	zassert_equal(g1_pt_uart_tx_log.len[0], sizeof(b), NULL);
	zassert_mem_equal(g1_pt_uart_tx_log.data[0], b, sizeof(b), NULL);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_comm_write_unknown_transport_sends_nothing)
{
	uint8_t b[1] = {0x24};

	zassert_equal(comm_write(3, (void *)&nfc_dev, b, sizeof(b)), 0, NULL);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.call_count, 0, NULL);
	zassert_equal(g1_pt_uart_tx_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_comm_read_rejects_bad_arguments)
{
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len;

	zassert_equal(comm_read(PT_COMM_NFC, NULL, &p, &len), -1, NULL);
	zassert_equal(comm_read(PT_COMM_NFC, (void *)&nfc_dev, NULL, &len), -1, NULL);
	zassert_equal(comm_read(PT_COMM_NFC, (void *)&nfc_dev, &p, NULL), -1, NULL);
	zassert_equal(comm_read(PT_COMM_NONE, (void *)&nfc_dev, &p, &len), -1, NULL);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_comm_read_nfc_takes_message_from_mailbox)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len = 0;

	mailbox_put(req, sizeof(req));

	zassert_equal(comm_read(PT_COMM_NFC, (void *)&nfc_dev, &p, &len), 0, NULL);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.arg0_val, &nfc_dev, NULL);
	zassert_equal(len, sizeof(req), NULL);
	zassert_mem_equal(buf, req, sizeof(req), NULL);
}

ZTEST(production_test, test_comm_read_uart_takes_payload_of_serial_frame)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x04, 0x24, 0x01, 0x01, 0x00, 0xa8};
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(comm_read(PT_COMM_UART, (void *)&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(len, 4, NULL);
	zassert_mem_equal(buf, &frame[4], 4, NULL);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_transport_setters_reject_null)
{
	zassert_equal(setNFCDevice(NULL), -1, NULL);
	zassert_equal(setUARTDevice(NULL), -1, NULL);
	zassert_equal(setNFCDevice((void *)&nfc_dev), 0, NULL);
	zassert_equal(setUARTDevice(NULL), -1, NULL);
	zassert_equal(pt_comm.type, PT_COMM_NFC, "a rejected device does not switch transport");
}

ZTEST(production_test, test_sensor_mutex_locks_nfc_tag_when_set)
{
	lock_sensor_mutex();
	unlock_sensor_mutex();
	zassert_equal(eeprom_st25dv_lock_fake.call_count, 0, NULL);
	zassert_equal(eeprom_st25dv_unlock_fake.call_count, 0, NULL);

	setNFCDevice((void *)&nfc_dev);
	lock_sensor_mutex();
	unlock_sensor_mutex();
	zassert_equal(eeprom_st25dv_lock_fake.call_count, 1, NULL);
	zassert_equal(eeprom_st25dv_lock_fake.arg0_val, &nfc_dev, NULL);
	zassert_equal(eeprom_st25dv_unlock_fake.call_count, 1, NULL);
}

ZTEST(production_test, test_pt_without_transport_returns_minus_1)
{
	zassert_equal(process_pt_data(NULL), -1, NULL);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.call_count, 0, NULL);
	zassert_equal(pt_comm_busy_set_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_pt_mailbox_read_error_returns_minus_2)
{
	setNFCDevice((void *)&nfc_dev);
	eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.custom_fake = NULL;
	eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.return_val = -3;

	zassert_equal(process_pt_data(NULL), -2, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 0, NULL);
	zassert_equal(pt_comm_busy_set_fake.call_count, 1, NULL);
	zassert_equal(pt_comm_busy_clear_fake.call_count, 1, NULL);
}

/* 01 06 06 = VERSION */
ZTEST(production_test, test_pt_version_request_over_nfc)
{
	static const uint8_t req[] = {0x24, 0x01, 0x01, 0x00};
	static const uint8_t exp[] = {0x24, 0x01, 0x03, 0x03, 0x01, 0x06, 0x06, 0x38};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 1, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.arg0_val, &nfc_dev, NULL);
	zassert_equal(pt_comm_busy_set_fake.call_count, 1, NULL);
	zassert_equal(pt_comm_busy_clear_fake.call_count, 1, NULL);
}

ZTEST(production_test, test_pt_unknown_command_replies_status_2)
{
	static const uint8_t req[] = {0x77, 0x01, 0x01, 0x00};
	static const uint8_t exp[] = {0x77, 0x01, 0x03, 0x01, 0x02, 0x7e};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
}

ZTEST(production_test, test_pt_empty_request_is_not_answered)
{
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(NULL, 0);

	zassert_equal(process_pt_data(NULL), -3, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 0, NULL);
	zassert_equal(pt_comm_busy_clear_fake.call_count, 1, NULL);
}

ZTEST(production_test, test_pt_mailbox_write_error_returns_minus_4)
{
	static const uint8_t req[] = {0x24, 0x01, 0x01, 0x00};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));
	eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.custom_fake = NULL;
	eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.return_val = -3;

	zassert_equal(process_pt_data(NULL), -4, NULL);
	zassert_equal(pt_comm_busy_clear_fake.call_count, 1, NULL);
}

/* then the stray 0x5a */
ZTEST(production_test, test_pt_version_request_over_uart_reply_is_framed)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x04, 0x24, 0x01, 0x01, 0x00, 0xa8};
	static const uint8_t exp[] = {0x5a, 0xa5, 0xff, 0x07, 0x24, 0x01, 0x03, 0x03,
				      0x01, 0x06, 0x06, 0x3d};

	zassert_equal(setUARTDevice(&g1_pt_uart_dev), 0, NULL);
	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(g1_pt_uart_tx_log.count, 2, NULL);
	zassert_equal(g1_pt_uart_tx_log.len[0], sizeof(exp), NULL);
	zassert_mem_equal(g1_pt_uart_tx_log.data[0], exp, sizeof(exp), NULL);
	zassert_equal(g1_pt_uart_tx_log.data[1][0], 0x5a, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 0, NULL);
}

/* DEFECT: a refusing handler leaves the reply empty: a lone 00 checksum is sent. */
ZTEST_EXPECT_FAIL(production_test, test_pt_refused_command_reply_names_the_command);
ZTEST(production_test, test_pt_refused_command_reply_names_the_command)
{
	static const uint8_t req[] = {0x32, 0x01, 0x01, 0x01, 0x01};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 1, NULL);
	zassert_true(g1_pt_mailbox_tx.len[0] >= 6, "reply is %d byte(s), first 0x%02x",
		     g1_pt_mailbox_tx.len[0], g1_pt_mailbox_tx.data[0][0]);
	zassert_equal(g1_pt_mailbox_tx.data[0][0], 0x32, NULL);
	zassert_not_equal(g1_pt_mailbox_tx.data[0][4], 0, NULL);
}

/* DEFECT (skipped, corrupts memory): 0x1e builds an 0x52-byte reply in the 50-byte pt_resp_buf. */
ZTEST(production_test, test_pt_flash_info_reply_fits_response_buffer)
{
	static const uint8_t req[] = {0x1e, 0x01, 0x01, 0x00};

	ztest_test_skip();

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));
	g1_state()->flash_read = NULL; /* no flash API on the host */
	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(g1_pt_mailbox_tx.len[0], 0x52 + 1, NULL);
}

/* status, field mV (BE), SoC %, 1 = discharging, |mA| */
ZTEST(production_test, test_pt_case_battery_query_reports_charging_state)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};
	static const uint8_t exp[] = {0x13, 0x01, 0x02, 0x06, 0x00, 0x13, 0x88,
				      0x32, 0x00, 0x64, 0x4d};

	battery_charging();
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 1, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
}

ZTEST(production_test, test_pt_case_battery_status_ok_enables_charger)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};

	battery_charging();
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(enable_charger_fake.call_count, 1, NULL);
}

ZTEST(production_test, test_pt_case_battery_charger_error_returns_minus_5)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};

	battery_charging();
	enable_charger_fake.return_val = -EIO;
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), -5, NULL);
	zassert_equal(g1_pt_mailbox_tx.count, 1, NULL);
}

ZTEST(production_test, test_pt_case_battery_over_temperature_does_not_enable_charger)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};
	static const uint8_t exp[] = {0x13, 0x01, 0x02, 0x06, 0x02, 0x13, 0x88,
				      0x32, 0x00, 0x64, 0x4f};

	battery_charging();
	g1_state()->battery_temp_x100 = 6000;
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(enable_charger_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_pt_case_battery_full_does_not_enable_charger)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};
	static const uint8_t exp[] = {0x13, 0x01, 0x02, 0x06, 0x01, 0x13, 0x88,
				      0x64, 0x00, 0x64, 0x80};

	battery_charging();
	g1_state()->battery_left = 100;
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(enable_charger_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_pt_case_battery_discharging_sets_direction_byte)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};

	battery_charging();
	g1_state()->battery_current_ma = -100;
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	zassert_equal(g1_pt_mailbox_tx.data[0][8], 1, NULL);
	zassert_equal(g1_pt_mailbox_tx.data[0][9], 100, NULL);
}

/* Field above 5.5 V on four polls: byte 9 = 0x80, resent 1 s later cleared. */
static void four_high_field_polls(void)
{
	static const uint8_t req[] = {0x13, 0x01, 0x02, 0x00};

	battery_charging();
	nfc_get_field_mv_fake.return_val = 6000;
	setNFCDevice((void *)&nfc_dev);
	g1_clock_advance_ms(10001);
	for (int i = 0; i < 4; i++) {
		mailbox_put(req, sizeof(req));
		zassert_equal(process_pt_data(NULL), 0, NULL);
	}
}

ZTEST(production_test, test_pt_case_battery_flagged_reply_is_sent_again_after_1s)
{
	static const uint8_t flagged[] = {0x13, 0x01, 0x02, 0x06, 0x00, 0x17, 0x70,
					  0x32, 0x00, 0x80, 0x55};
	int64_t start = g1_clock_uptime_ms();

	four_high_field_polls();

	zassert_equal(g1_pt_mailbox_tx.count, 5, "3 plain replies, then 2 for the fourth poll");
	zassert_equal(g1_pt_mailbox_tx.data[2][9], 0x64, "third poll is not flagged");
	expect_mailbox_reply(3, flagged, sizeof(flagged));
	zassert_equal(g1_pt_mailbox_tx.len[4], sizeof(flagged), NULL);
	zassert_mem_equal(g1_pt_mailbox_tx.data[4], flagged, 9, NULL);
	zassert_equal(g1_pt_mailbox_tx.data[4][9], 0x00, NULL);
	zassert_true(g1_clock_uptime_ms() - start >= 10001 + 1000, NULL);
}

/* DEFECT: byte 9 is cleared after the checksum, so the resent sum is wrong. */
ZTEST_EXPECT_FAIL(production_test, test_pt_case_battery_repeated_reply_has_valid_checksum);
ZTEST(production_test, test_pt_case_battery_repeated_reply_has_valid_checksum)
{
	four_high_field_polls();

	zassert_equal(g1_pt_mailbox_tx.count, 5, NULL);
	zassert_equal(g1_pt_mailbox_tx.data[4][10], pt_test_sum8(g1_pt_mailbox_tx.data[4], 10),
		      "sum byte 0x%02x", g1_pt_mailbox_tx.data[4][10]);
}

ZTEST(production_test, test_pt_long_trip_from_case_arms_low_power)
{
	static const uint8_t req[] = {0x06, 0x01, 0x02, 0x00};
	static const uint8_t exp[] = {0x06, 0x01, 0x02, 0x01, 0x00, 0x0a};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(can_enter_low_power, 1, NULL);
	zassert_equal(set_box_status_fake.arg0_val, BOX_LID_CLOSED, NULL);
	zassert_equal(set_box_status_fake.arg1_val, 1, NULL);
	zassert_equal(nfc_deadline_set_fake.arg0_val, 10000, NULL);
	zassert_equal(change_work_mode_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_pt_long_trip_from_tool_enters_work_mode_7_after_10s)
{
	static const uint8_t req[] = {0x06, 0x01, 0x01, 0x00};
	static const uint8_t exp[] = {0x06, 0x01, 0x03, 0x01, 0x00, 0x0b};
	int64_t start = g1_clock_uptime_ms();

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(change_work_mode_fake.call_count, 1, NULL);
	zassert_equal(change_work_mode_fake.arg0_val, 7, NULL);
	zassert_true(g1_clock_uptime_ms() - start >= 10000, NULL);
	zassert_equal(can_enter_low_power, 0, NULL);
}

ZTEST(production_test, test_pt_restart_replies_then_reboots)
{
	static const uint8_t req[] = {0x0b, 0x01, 0x01, 0x00};
	static const uint8_t exp[] = {0x0b, 0x01, 0x03, 0x01, 0x00, 0x10};
	int64_t start = g1_clock_uptime_ms();

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	G1_PT_EXPECT_REBOOT(process_pt_data(NULL));

	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(g1_pt_reboot_count, 1, NULL);
	zassert_equal(g1_pt_reboot_type, SYS_REBOOT_COLD, NULL);
	zassert_true(g1_pt_reboot_uptime_ms - start >= 300, NULL);
}

ZTEST(production_test, test_pt_enter_testmode_saves_and_reboots)
{
	static const uint8_t req[] = {0x01, 0x01, 0x01, 0x01, 0x01};
	static const uint8_t exp[] = {0x01, 0x01, 0x03, 0x01, 0x00, 0x06};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	G1_PT_EXPECT_REBOOT(process_pt_data(NULL));

	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(get_pt_test_mode(), 1, NULL);
	zassert_equal(save_usr_settings_fake.call_count, 1, NULL);
	zassert_equal(g1_pt_reboot_type, SYS_REBOOT_COLD, NULL);
}

ZTEST(production_test, test_pt_enter_testmode_already_set_does_not_reboot)
{
	static const uint8_t req[] = {0x01, 0x01, 0x01, 0x01, 0x01};
	static const uint8_t exp[] = {0x01, 0x01, 0x03, 0x01, 0x04, 0x0a};

	pt_test_mode = 1;
	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(save_usr_settings_fake.call_count, 0, NULL);
	zassert_equal(g1_pt_reboot_count, 0, NULL);
}

ZTEST(production_test, test_pt_enter_testmode_invalid_value_is_refused)
{
	static const uint8_t req[] = {0x01, 0x01, 0x01, 0x01, 0x02};
	static const uint8_t exp[] = {0x01, 0x01, 0x03, 0x01, 0x03, 0x09};

	setNFCDevice((void *)&nfc_dev);
	mailbox_put(req, sizeof(req));

	zassert_equal(process_pt_data(NULL), 0, NULL);
	expect_mailbox_reply(0, exp, sizeof(exp));
	zassert_equal(get_pt_test_mode(), 0, NULL);
	zassert_equal(g1_pt_reboot_count, 0, NULL);
}
