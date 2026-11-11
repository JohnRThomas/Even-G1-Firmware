/*
 * Link and session commands: 0x37 uptime, 0x4D MTU / session start, 0x2C device info, 0x39 running
 * app, 0x22 status, 0x3D language, 0x47 unpair, 0x17 erase DFU, 0x50 dashboard lock.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/atomic.h>
#include <app_version.h>

#include "harness.h"
#include "session.h"
#include "fakes_bt.h"
#include "fakes_display.h"
#include "fakes_esb.h"
#include "fakes_misc.h"

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

	expect_frame_padded("37 37 8d 0e 00 00 00", 20);
	expect_no_frame();
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
	expect_frame_padded("37 37 42 00 00 00 00", 20);
	expect_no_frame();
}

/* The MTU byte is unused; the ATT MTU comes from the stack's exchange. */
ZTEST(proto_session, test_0x4d_accepts_any_mtu_value)
{
	static const uint8_t values[] = {0x00, 0x17, 0xfb, 0xff};

	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		send_vector_with("0x4d_mtu_251", 1, values[i]);
		expect_frame_padded("4d c9", 20);
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

ZTEST(proto_session, test_0x2c_sub_1_marks_the_phone_as_android)
{
	g1_state()->phone_is_ios = 1;

	g1_send_vector("0x2c_android");

	zassert_equal(g1_state()->phone_is_ios, 0);
	zassert_equal(g1_frame_count(), 1);
}

ZTEST(proto_session, test_0x2c_sub_2_marks_the_phone_as_ios)
{
	g1_send_vector("0x2c_ios");

	zassert_equal(g1_state()->phone_is_ios, 1);
	zassert_equal(g1_frame_count(), 1);
}

ZTEST(proto_session, test_0x2c_other_sub_leaves_the_phone_type_unchanged)
{
	g1_state()->phone_is_ios = 1;
	send_vector_with("0x2c_android", 1, 0x05);
	zassert_equal(g1_state()->phone_is_ios, 1);

	g1_state()->phone_is_ios = 0;
	send_vector_with("0x2c_android", 1, 0x00);
	zassert_equal(g1_state()->phone_is_ios, 0);
	zassert_equal(g1_frame_count(), 2);
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
	struct GlassesState *s = g1_state();
	const uint8_t want[20] = {0x2c, 0x66, 0x50, 0x3c, 0xee, 0x84, 0x1a,
				  APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL,
				  0x01, 0x06, 0x05};

	s->battery_left = 0x50;
	s->battery_right = 0x3c;
	s->battery_voltage = 0xee;
	s->battery_current = 0x84;
	s->battery_temp = 0x1a;
	s->fw_ver.major = 1;
	s->fw_ver.minor = 6;
	s->fw_ver.patch = 5;

	g1_send_vector("0x2c_android");

	expect_frame_bytes(want, sizeof(want));
	expect_no_frame();
}

ZTEST(proto_session, test_0x2c_slave_reply_has_no_master_version_and_its_own_version)
{
	struct GlassesState *s;
	const uint8_t want[20] = {0x2c, 0x66, 0x58, 0x00, 0xe2, 0x88, 0x1f, 0x00, 0x00, 0x00,
				  APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL};

	g1_unit_set_role(G1_ROLE_SLAVE);
	s = g1_state();
	s->battery_left = 0x58;
	s->battery_right = 0x00;
	s->battery_voltage = 0xe2;
	s->battery_current = 0x88;
	s->battery_temp = 0x1f;

	g1_send_vector("0x2c_android");

	expect_frame_bytes(want, sizeof(want));
	expect_no_frame();
}

/* Defect: the second battery reads 100 whenever the own battery is above 96. */
ZTEST_EXPECT_FAIL(proto_session, test_0x2c_other_battery_is_not_replaced_by_a_full_own_battery);
ZTEST(proto_session, test_0x2c_other_battery_is_not_replaced_by_a_full_own_battery)
{
	size_t len;
	const uint8_t *f;

	g1_state()->battery_left = 100;
	g1_state()->battery_right = 50;

	g1_send_vector("0x2c_android");

	f = g1_frame_peek(0, &len);
	zassert_not_null(f);
	zassert_equal(f[2], 0x64);
	zassert_equal(f[3], 0x32, "other lens reported as 0x%02x", f[3]);
}

ZTEST(proto_session, test_0x39_running_app_echoes_request_plus_zero_when_idle)
{
	is_idle_fake.return_val = true;
	g1_state()->dashboard_mode = 6;

	g1_send_vector("0x39_running_app");

	expect_frame("39 05 00 02 01 00");
	expect_no_frame();
}

ZTEST(proto_session, test_0x39_running_app_reports_the_running_screen)
{
	is_idle_fake.return_val = false;
	g1_state()->dashboard_mode = 6;

	send_vector_with("0x39_running_app", 3, 0x4a);

	expect_frame("39 05 00 4a 01 06");
	expect_no_frame();
}

ZTEST(proto_session, test_0x39_length_mismatch_replies_ff)
{
	is_idle_fake.return_val = true;

	g1_send_hex("39 06 00 02 01");

	expect_frame("39 06 00 02 01 ff");
	expect_no_frame();
}

/* Reply: echo, unread count, note index + 1, low-power flag. */
ZTEST(proto_session, test_0x22_status_reports_unread_count_note_index_and_low_power)
{
	send_vector_with("0x22_status_get", 3, 0x13);
	expect_frame("22 05 00 13 01 00 01 00");

	g1_state()->unread_msg_num = 3;
	getNotesIndex_fake.return_val = 1;
	g1_state()->dashboard_ts->low_power = 1;
	g1_send_vector("0x22_status_get");
	expect_frame("22 05 00 0a 01 03 02 01");
	expect_no_frame();
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

	expect_frame("3d 06 00 1a 01 02");
	expect_no_frame();
	zassert_equal(SendSystemLanguageInfoToSlave_fake.call_count, 1);
	zassert_equal(SendSystemLanguageInfoToSlave_fake.arg0_val, 2);
	zassert_equal(CURRENT_LANGUAGE.magic, 2);
}

ZTEST(proto_session, test_0x3d_slave_echoes_without_forwarding)
{
	g1_unit_set_role(G1_ROLE_SLAVE);

	g1_send_vector("0x3d_language_en");

	expect_frame("3d 06 00 1a 01 02");
	expect_no_frame();
	zassert_equal(SendSystemLanguageInfoToSlave_fake.call_count, 0);
	zassert_equal(CURRENT_LANGUAGE.magic, 2);
}

ZTEST(proto_session, test_0x3d_unknown_language_id_is_echoed_and_the_ui_language_kept)
{
	uint8_t before = CURRENT_LANGUAGE.language;

	send_vector_with("0x3d_language_en", 5, 0x63);

	expect_frame("3d 06 00 1a 01 63");
	expect_no_frame();
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

	expect_frame_padded("47 c9", 20);
	expect_no_frame();
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

	expect_frame_padded("17 c9", 20);
	expect_no_frame();
	zassert_equal(setEraseDFU_fake.call_count, 1);
	zassert_true(setEraseDFU_fake.arg0_val);
	zassert_equal(g1_state()->dashboard_cmd, 0x17);
	zassert_true(dashboard_notified());
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

ZTEST(proto_session, test_0x50_slave_ignores_the_lock)
{
	g1_unit_set_role(G1_ROLE_SLAVE);

	g1_send_vector("0x50_dashboard_lock");

	expect_no_frame();
	zassert_false(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
}

ZTEST(proto_session, test_0x50_other_action_does_not_lock)
{
	send_vector_with("0x50_dashboard_lock", 4, 0x02);

	expect_no_frame();
	zassert_false(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
}

/* length field 7, packet 6 */
ZTEST(proto_session, test_0x50_length_mismatch_does_not_lock)
{
	send_vector_with("0x50_dashboard_lock", 1, 0x07);

	expect_no_frame();
	zassert_false(atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT));
}
