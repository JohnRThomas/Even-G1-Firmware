/*
 * User settings set by the app and read back. The simple setters always reply <cmd> C9 and hand a
 * changed value to the dashboard thread.
 */
#include <zephyr/ztest.h>

#include "harness.h"
#include "session.h"
#include "fakes_bt.h"
#include "fakes_display.h"
#include "fakes_esb.h"
#include "fakes_misc.h"

#include "storage/storage.h"

ZTEST(proto_session, test_0x01_sets_level_and_auto_and_replies_c9)
{
	g1_send_vector("0x01_brightness_auto"); /* 01 2a 01 */

	expect_frame_padded("01 c9", 20);
	expect_no_frame();
	zassert_equal(g1_fake_panel_set_brightness_fake.call_count, 1);
	zassert_equal(g1_fake_panel_set_brightness_fake.arg1_val, 0x2a);
	zassert_equal(g1_state()->panel.panel_brightness_level, 0x2a);
	zassert_equal(g1_state()->opt3007_context.auto_brightness, 1);
	zassert_equal(g1_state()->dashboard_cmd, 0x01);
	zassert_true(dashboard_notified());
}

ZTEST(proto_session, test_0x29_reads_back_level_and_auto_set_by_0x01)
{
	g1_send_hex("01 13 01");
	expect_frame_padded("01 c9", 20);

	g1_send_vector("0x29_brightness_get");

	expect_frame_padded("29 65 13 01", 20);
	expect_no_frame();
}

ZTEST(proto_session, test_0x01_same_values_do_not_notify_the_dashboard)
{
	g1_state()->panel.panel_brightness_level = 0x13;
	g1_state()->opt3007_context.auto_brightness = 1;

	g1_send_hex("01 13 01");

	expect_frame_padded("01 c9", 20);
	zassert_equal(g1_fake_panel_set_brightness_fake.call_count, 0);
	zassert_false(dashboard_notified());
}

/* Defect: no range check; 0x2B and above reach the panel. */
ZTEST_EXPECT_FAIL(proto_session, test_0x01_level_above_0x2a_is_rejected);
ZTEST(proto_session, test_0x01_level_above_0x2a_is_rejected)
{
	g1_state()->panel.panel_brightness_level = 0x13;

	g1_send_hex("01 2b 00");

	expect_frame_padded("01 ca", 20);
	zassert_equal(g1_state()->panel.panel_brightness_level, 0x13);
}

ZTEST(proto_session, test_0x2a_reads_back_anti_shake_set_by_0x02)
{
	g1_send_hex("02 01");
	expect_frame_padded("02 c9", 20);
	zassert_true(dashboard_notified());

	g1_send_vector("0x2a_anti_shake_get");
	expect_frame_padded("2a 68 01", 20);

	g1_send_hex("02 00");
	expect_frame_padded("02 c9", 20);
	g1_send_vector("0x2a_anti_shake_get");
	expect_frame_padded("2a 68 00", 20);
}

/*
 * Reply: 2B 69 <silent mode> <state>. Silent mode 0A off / 0C on; the state code is the wear / case
 * event: 06 worn, 07 not worn, 08 lid open, 0A lid closed, 0B lid closed and charging.
 */
ZTEST(proto_session, test_0x2b_reports_silent_mode_and_each_wear_and_case_state_code)
{
	static const struct {
		uint8_t display_mode;
		uint8_t state;
	} rows[] = {
		{0x0a, 0x06}, {0x0c, 0x07}, {0x0a, 0x08}, {0x0c, 0x0a}, {0x0a, 0x0b},
	};
	static char msg[G1_MSG_SIZE];

	for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
		const uint8_t want[] = {0x2b, 0x69, rows[i].display_mode, rows[i].state};
		char hex[32];

		g1_state()->display_mode = rows[i].display_mode;
		get_wear_state_event_fake.return_val = rows[i].state;

		g1_send_vector("0x2b_silent_mode_get");

		g1_hex_format(want, sizeof(want), hex, sizeof(hex));
		zassert_true(g1_frame_check(hex, 20, msg, sizeof(msg)),
			     "display_mode 0x%02x, state 0x%02x: %s", rows[i].display_mode,
			     rows[i].state, msg);
	}
	expect_no_frame();
}

ZTEST(proto_session, test_0x03_silent_on_master_requests_the_picture)
{
	g1_state()->display_mode = 0x0a;

	g1_send_vector("0x03_silent_mode_on"); /* 03 0c 01 */

	expect_frame_padded("03 c9", 20);
	zassert_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(request_not_disturb_bitmap_fake.call_count, 1);
	zassert_equal(request_not_disturb_bitmap_fake.arg0_val, 0);
	zassert_equal(show_not_disturb_bitmap_fake.call_count, 0);
	zassert_equal(g1_state()->dashboard_cmd, 0x03);
	zassert_true(dashboard_notified());

	g1_send_vector("0x2b_silent_mode_get");
	expect_frame_padded("2b 69 0c 00", 20);
}

ZTEST(proto_session, test_0x03_silent_on_slave_does_not_request_the_picture)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_state()->display_mode = 0x0a;

	g1_send_vector("0x03_silent_mode_on");

	expect_frame_padded("03 c9", 20);
	zassert_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(request_not_disturb_bitmap_fake.call_count, 0);
	zassert_true(dashboard_notified());
}

static void silent_off_shows_the_picture(int role)
{
	g1_unit_set_role(role);
	g1_state()->display_mode = 0x0c;

	g1_send_vector("0x03_silent_mode_off"); /* 03 0a 00 */

	expect_frame_padded("03 c9", 20);
	zassert_equal(g1_state()->display_mode, 0x0a);
	zassert_equal(show_not_disturb_bitmap_fake.call_count, 1);
	zassert_equal(show_not_disturb_bitmap_fake.arg0_val, 1);
	zassert_true(dashboard_notified());
}

ZTEST(proto_session, test_0x03_silent_off_master_shows_the_picture)
{
	silent_off_shows_the_picture(G1_ROLE_MASTER);
}

ZTEST(proto_session, test_0x03_silent_off_slave_shows_the_picture)
{
	silent_off_shows_the_picture(G1_ROLE_SLAVE);
}

ZTEST(proto_session, test_0x03_silent_off_with_byte_2_set_skips_the_picture)
{
	g1_state()->display_mode = 0x0c;

	g1_send_vector("0x03_silent_mode_off_no_picture"); /* 03 0a 01 */

	expect_frame_padded("03 c9", 20);
	zassert_equal(g1_state()->display_mode, 0x0a);
	zassert_equal(show_not_disturb_bitmap_fake.call_count, 0);
}

ZTEST(proto_session, test_0x03_same_mode_does_not_notify_the_slave)
{
	g1_state()->display_mode = 0x0a;

	g1_send_vector("0x03_silent_mode_off");

	expect_frame_padded("03 c9", 20);
	zassert_false(dashboard_notified());
	zassert_equal(g1_state()->dashboard_cmd, 0);
	zassert_equal(show_not_disturb_bitmap_fake.call_count, 0);
	zassert_equal(request_not_disturb_bitmap_fake.call_count, 0);
}

ZTEST(proto_session, test_0x03_invalid_mode_leaves_the_mode_unchanged)
{
	g1_state()->display_mode = 0x0a;

	g1_send_hex("03 05 00");

	zassert_equal(g1_state()->display_mode, 0x0a);
	zassert_false(dashboard_notified());
}

/* Defect: the generic PUT path ignores the handler's CA and always replies C9. */
ZTEST_EXPECT_FAIL(proto_session, test_0x03_invalid_mode_is_answered_ca);
ZTEST(proto_session, test_0x03_invalid_mode_is_answered_ca)
{
	g1_state()->display_mode = 0x0a;

	g1_send_hex("03 05 00");

	expect_frame_padded("03 ca", 20);
}

ZTEST(proto_session, test_0x32_reads_back_the_angle_set_by_0x0b)
{
	g1_send_vector("0x0b_head_up_angle"); /* 0b 14 01 */

	expect_frame_padded("0b c9", 20);
	zassert_equal(g1_state()->imu.level, 0x14);
	zassert_equal(g1_state()->imu.wear, 1);
	zassert_true(dashboard_notified());

	g1_send_vector("0x32_head_up_angle_get");
	expect_frame_padded("32 6d 14 01", 20);
	expect_no_frame();
}

/* Defect: no angle range check (0..60). */
ZTEST_EXPECT_FAIL(proto_session, test_0x0b_angle_above_60_is_rejected);
ZTEST(proto_session, test_0x0b_angle_above_60_is_rejected)
{
	g1_state()->imu.level = 0x14;

	g1_send_hex("0b 3d 01");

	expect_frame_padded("0b ca", 20);
	zassert_equal(g1_state()->imu.level, 0x14);
}

ZTEST(proto_session, test_0x3a_reads_back_wear_detection_set_by_0x27)
{
	g1_send_vector("0x27_wear_detection_on");
	expect_frame_padded("27 c9", 20);
	zassert_equal(g1_state()->check_mode, 1);
	zassert_equal(g1_state()->dashboard_cmd, 0x27);
	zassert_true(dashboard_notified());

	g1_send_vector("0x3a_wear_detection_get");
	expect_frame_padded("3a c9 01", 20);

	send_vector_with("0x27_wear_detection_on", 1, 0x00);
	expect_frame_padded("27 c9", 20);
	g1_send_vector("0x3a_wear_detection_get");
	expect_frame_padded("3a c9 00", 20);
	expect_no_frame();
}

/* byte 3 is checked by test_0x4f_reply_byte_3_is_zero */
static void expect_auto_display_reply(uint8_t enable)
{
	uint8_t want[20] = {0x4f, 0xc9, enable};
	uint8_t got[20];
	char txt[80];
	int len = g1_frame_pop(got, sizeof(got));

	zassert_equal(len, 20, "reply of %d bytes", len);
	got[3] = 0;
	g1_hex_format(got, sizeof(got), txt, sizeof(txt));
	zassert_mem_equal(got, want, sizeof(want), "got %s", txt);
}

ZTEST(proto_session, test_0x4f_sets_auto_display_and_replies_with_the_enable_byte)
{
	g1_send_vector("0x4f_auto_display"); /* 4f 01 06 */

	expect_auto_display_reply(0x01);
	expect_no_frame();
	zassert_equal(g1_state()->message_mode, 1);
	zassert_equal(g1_state()->message_stay_time, 6);
	zassert_equal(g1_state()->dashboard_cmd, 0x4f);
	zassert_true(dashboard_notified());
}

static __noinline void poison_stack(void)
{
	volatile uint8_t area[4096];

	for (size_t i = 0; i < sizeof(area); i++) {
		area[i] = 0xa5;
	}
}

/*
 * Defect: byte 3 is an uninitialised stack byte. The stack is poisoned first so the outcome is
 * stable.
 */
ZTEST_EXPECT_FAIL(proto_session, test_0x4f_reply_byte_3_is_zero);
ZTEST(proto_session, test_0x4f_reply_byte_3_is_zero)
{
	size_t len;
	const uint8_t *f;

	poison_stack();
	g1_send_vector("0x4f_auto_display");

	f = g1_frame_peek(0, &len);
	zassert_not_null(f);
	zassert_equal(f[3], 0x00, "byte 3 is 0x%02x", f[3]);
}

ZTEST(proto_session, test_0x3c_reads_back_timeout_0_and_255_set_by_0x4f)
{
	g1_send_hex("4f 01 00");
	expect_auto_display_reply(0x01);
	g1_send_vector("0x3c_notification_auto_display_get");
	expect_frame_padded("3c c9 01 00", 20);

	g1_send_hex("4f 00 ff");
	expect_auto_display_reply(0x00);
	g1_send_vector("0x3c_notification_auto_display_get");
	expect_frame_padded("3c c9 00 ff", 20);
	expect_no_frame();
}

/* sub 4 = get: the request is echoed with byte 5 = the dashboard startup mode */
ZTEST(proto_session, test_0x08_get_echoes_the_request_with_the_startup_mode)
{
	g1_send_vector("0x08_head_up_action_get");
	expect_frame("08 06 00 00 04 00");

	CURRENT_LANGUAGE.startup_mode = 2;
	g1_send_vector("0x08_head_up_action_get");
	expect_frame("08 06 00 00 04 02");
	expect_no_frame();
}

/* 0 dashboard, 2 nothing */
ZTEST(proto_session, test_0x08_set_on_master_forwards_the_action_and_echoes)
{
	send_vector_with("0x08_head_up_action_set", 5, 0x02); /* 08 06 00 00 03 02 */

	expect_frame("08 06 00 00 03 02");
	expect_no_frame();
	zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.call_count, 1);
	zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.arg0_val, 2);
}

ZTEST(proto_session, test_0x08_set_on_slave_echoes_without_forwarding)
{
	g1_unit_set_role(G1_ROLE_SLAVE);

	g1_send_vector("0x08_head_up_action_set"); /* 08 06 00 00 03 00 */

	expect_frame("08 06 00 00 03 00");
	expect_no_frame();
	zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.call_count, 0);
}

/* the only reply is the echo of an applied action */
ZTEST(proto_session, test_0x08_unknown_action_is_not_forwarded)
{
	send_vector_with("0x08_head_up_action_set", 5, 0x03);

	zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.call_count, 0);
	expect_no_frame();
}

ZTEST(proto_session, test_0x08_length_mismatch_is_dropped)
{
	g1_send_hex("08 07 00 00 03 01");

	zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.call_count, 0);
	expect_no_frame();
}

/* Defect, skipped (stack overflow): data_len bytes are copied into a 20-byte reply. */
ZTEST(proto_session, test_0x08_long_packet_does_not_overflow_the_reply)
{
	uint8_t pkt[30] = {0x08, 30, 0x00, 0x00, 0x04, 0x00};

	ztest_test_skip();

	g1_send(pkt, sizeof(pkt));
	zassert_true(g1_frame_count() <= 1);
}
