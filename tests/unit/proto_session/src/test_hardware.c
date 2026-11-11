/*
 * Hardware Set 0x26 (reply 26 06 00 <seq> <sub> C9/CA), Hardware Get 0x3F and Raster Config Get
 * 0x3B.
 */
#include <zephyr/ztest.h>

#include "harness.h"
#include "session.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "fakes_esb.h"
#include "fakes_misc.h"

#include "storage/storage.h"

ZTEST(proto_session, test_0x3b_reports_height_and_depth)
{
	g1_state()->panel.raster_height_offset = 5;
	g1_state()->panel.canvas_distance_gear = 9;

	g1_send_vector("0x3b_raster_config_get");

	expect_frame_padded("3b c9 05 09", 20);
	expect_no_frame();
}

/* the main loop does the reset */
ZTEST(proto_session, test_0x26_sub_1_marks_the_user_data_for_reset)
{
	g1_send_hex("26 05 00 07 01");

	expect_frame_padded("26 06 00 07 01 c9", 20);
	expect_no_frame();
	zassert_equal(g1_state()->user_data_reset_pending, 1);
}

ZTEST(proto_session, test_0x26_sub_2_preview_applies_height_and_depth)
{
	struct GlassesState *s = g1_state();

	g1_send_vector("0x26_height_depth_preview"); /* 26 08 00 02 02 01 05 01 */

	expect_frame_padded("26 06 00 02 02 c9", 20);
	zassert_equal(s->panel.raster_height_offset, 5);
	zassert_equal(s->panel.canvas_distance_gear, 1);
	zassert_equal(s->raster_height_cfg[0], 1);
	zassert_equal(s->dashboard_ts->dashboard_on, 1);
	zassert_equal(s->esb_down_id, 2);
	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal(update_persist_task_status_fake.arg1_val, 15);
	zassert_equal(update_persist_task_status_fake.arg2_val, 2);

	g1_send_vector("0x3b_raster_config_get");
	expect_frame_padded("3b c9 05 01", 20);
}

/* preview 0 ends the preview without applying its own values */
ZTEST(proto_session, test_0x26_sub_2_commit_after_preview_keeps_the_previewed_values)
{
	g1_send_vector("0x26_height_depth_preview"); /* height 5, depth 1 */
	expect_frame_padded("26 06 00 02 02 c9", 20);
	update_persist_task_status_fake.call_count = 0;

	g1_send_vector("0x26_height_depth"); /* preview 0, height 6, depth 9 */

	expect_frame_padded("26 06 00 00 02 c9", 20);
	zassert_equal(g1_state()->raster_height_cfg[0], 0);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
	g1_send_vector("0x3b_raster_config_get");
	expect_frame_padded("3b c9 05 01", 20);
}

ZTEST(proto_session, test_0x26_sub_2_without_preview_does_not_change_height_and_depth)
{
	g1_state()->panel.raster_height_offset = 5;
	g1_state()->panel.canvas_distance_gear = 9;

	send_vector_with("0x26_height_depth", 6, 0x02); /* preview 0, height 2, depth 9 */

	expect_frame_padded("26 06 00 00 02 c9", 20);
	zassert_equal(g1_state()->panel.raster_height_offset, 5);
	zassert_equal(g1_state()->panel.canvas_distance_gear, 9);
}

ZTEST(proto_session, test_0x26_sub_2_height_above_8_or_depth_above_9_is_rejected)
{
	g1_state()->panel.raster_height_offset = 5;
	g1_state()->panel.canvas_distance_gear = 1;

	g1_send_hex("26 08 00 03 02 01 09 01");
	expect_frame_padded("26 06 00 03 02 ca", 20);

	g1_send_hex("26 08 00 04 02 01 08 0a");
	expect_frame_padded("26 06 00 04 02 ca", 20);

	zassert_equal(g1_state()->panel.raster_height_offset, 5);
	zassert_equal(g1_state()->panel.canvas_distance_gear, 1);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
}

ZTEST(proto_session, test_0x26_sub_3_gives_the_mic_semaphore_in_production_mode_only)
{
	g1_send_hex("26 05 00 01 03");
	expect_frame_padded("26 06 00 01 03 c9", 20);
	zassert_equal(k_sem_count_get(&mic_transm_sem), 0);

	get_pt_test_mode_fake.return_val = 1;
	g1_send_hex("26 05 00 02 03");
	expect_frame_padded("26 06 00 02 03 c9", 20);
	zassert_equal(k_sem_count_get(&mic_transm_sem), 1);
}

ZTEST(proto_session, test_0x26_sub_4_sets_the_luminance_gear)
{
	g1_send_hex("26 06 00 08 04 20");

	expect_frame_padded("26 06 00 08 04 c9", 20);
	zassert_equal(g1_state()->panel.lum_gear, 0x20);
	zassert_equal(g1_state()->panel.brightness_max, 0x20);
	zassert_equal(sys_settings_dirty, 1);
	zassert_equal(g1_state()->dashboard_cmd, 0x26);
	zassert_true(dashboard_notified());
}

ZTEST(proto_session, test_0x26_sub_4_gear_above_0x3f_is_rejected)
{
	g1_state()->panel.lum_gear = 0x20;

	g1_send_hex("26 06 00 08 04 40");

	expect_frame_padded("26 06 00 08 04 ca", 20);
	zassert_equal(g1_state()->panel.lum_gear, 0x20);
	zassert_false(dashboard_notified());
}

/* a changed action goes to the slave; it is stored once the lenses agree */
ZTEST(proto_session, test_0x26_sub_5_forwards_a_changed_double_tap_action)
{
	g1_state()->double_tap_customize = 0;

	g1_send_hex("26 06 00 09 05 04");

	expect_frame_padded("26 06 00 09 05 c9", 20);
	zassert_equal(SendDoubleTapCustomizeToSlave_fake.call_count, 1);
	zassert_equal(SendDoubleTapCustomizeToSlave_fake.arg0_val, 4);
}

ZTEST(proto_session, test_0x26_sub_5_same_action_is_not_forwarded)
{
	g1_state()->double_tap_customize = 4;

	g1_send_hex("26 06 00 09 05 04");

	expect_frame_padded("26 06 00 09 05 c9", 20);
	zassert_equal(SendDoubleTapCustomizeToSlave_fake.call_count, 0);
}

ZTEST(proto_session, test_0x26_sub_5_action_above_5_is_rejected)
{
	g1_send_hex("26 06 00 09 05 06");

	expect_frame_padded("26 06 00 09 05 ca", 20);
	zassert_equal(SendDoubleTapCustomizeToSlave_fake.call_count, 0);
}

ZTEST(proto_session, test_0x3f_sub_5_reports_the_double_tap_action)
{
	g1_send_vector("0x3f_double_tap_get");
	expect_frame_padded("3f 05 c9 00", 20);

	g1_state()->double_tap_customize = 4;
	g1_send_vector("0x3f_double_tap_get");
	expect_frame_padded("3f 05 c9 04", 20);
	expect_no_frame();
}

/* 06 01 enters the calibration page, 06 02 <percent> sets the coefficient, 06 00 leaves */
ZTEST(proto_session, test_0x26_sub_6_enter_then_set_the_brightness_coefficient)
{
	g1_state()->panel.panel_brightness_level = 0x0c;

	g1_send_vector("0x26_lum_coefficient_enter");
	expect_frame_padded("26 06 00 10 06 c9", 20);
	zassert_equal(g1_state()->dashboard_ts->dashboard_hold, 1);

	g1_send_vector("0x26_lum_coefficient_set"); /* 100 % */
	expect_frame_padded("26 06 00 11 06 c9", 20);
	zassert_equal(g1_state()->panel.brightness_percent, 100);
	zassert_equal(set_brightness_to_panel_reg_in_running_fake.call_count, 1);
	zassert_equal(set_brightness_to_panel_reg_in_running_fake.arg0_val, 0x0c);
	/* storage commit request */
	zassert_true(dashboard_notified());
}

ZTEST(proto_session, test_0x3f_sub_6_reports_the_brightness_coefficient)
{
	g1_state()->panel.brightness_percent = 100;

	g1_send_vector("0x3f_lum_coefficient_get");

	expect_frame_padded("3f 06 c9 64", 20);
	expect_no_frame();
}

ZTEST(proto_session, test_0x26_sub_6_set_without_enter_is_rejected)
{
	g1_send_vector("0x26_lum_coefficient_set");

	expect_frame_padded("26 06 00 11 06 ca", 20);
	zassert_equal(g1_state()->panel.brightness_percent, 0);
	zassert_equal(set_brightness_to_panel_reg_in_running_fake.call_count, 0);
}

ZTEST(proto_session, test_0x26_sub_6_coefficient_outside_70_to_100_is_rejected)
{
	g1_state()->dashboard_ts->dashboard_hold = 1;
	g1_state()->panel.brightness_percent = 90;

	send_vector_with("0x26_lum_coefficient_set", 6, 69);
	expect_frame_padded("26 06 00 11 06 ca", 20);
	send_vector_with("0x26_lum_coefficient_set", 6, 101);
	expect_frame_padded("26 06 00 11 06 ca", 20);

	zassert_equal(g1_state()->panel.brightness_percent, 90);
}

ZTEST(proto_session, test_0x26_sub_6_exit_leaves_the_calibration_page)
{
	g1_state()->dashboard_ts->dashboard_hold = 1;

	send_vector_with("0x26_lum_coefficient_enter", 5, 0x00);

	expect_frame_padded("26 06 00 10 06 c9", 20);
	zassert_equal(g1_state()->dashboard_ts->dashboard_hold, 0);
}

ZTEST(proto_session, test_0x26_sub_6_unknown_mode_is_rejected)
{
	send_vector_with("0x26_lum_coefficient_enter", 5, 0x03);

	expect_frame_padded("26 06 00 10 06 ca", 20);
}

ZTEST(proto_session, test_0x26_sub_7_forwards_a_changed_long_press_switch)
{
	g1_send_hex("26 06 00 0a 07 01");

	expect_frame_padded("26 06 00 0a 07 c9", 20);
	zassert_equal(SendLongpressTapCustomizeToSlave_fake.call_count, 1);
	zassert_equal(SendLongpressTapCustomizeToSlave_fake.arg0_val, 1);
}

ZTEST(proto_session, test_0x26_sub_7_value_above_1_is_rejected)
{
	g1_send_hex("26 06 00 0a 07 02");

	expect_frame_padded("26 06 00 0a 07 ca", 20);
	zassert_equal(SendLongpressTapCustomizeToSlave_fake.call_count, 0);
}

ZTEST(proto_session, test_0x3f_sub_7_reports_the_long_press_switch)
{
	g1_send_vector("0x3f_long_press_get");
	expect_frame_padded("3f 07 c9 00", 20);

	g1_state()->longpress_tap_customize = 1;
	g1_send_vector("0x3f_long_press_get");
	expect_frame_padded("3f 07 c9 01", 20);
}

/* byte 5 = 1 disables the mic on head-up (inverted) */
ZTEST(proto_session, test_0x26_sub_8_sets_the_mic_on_head_up_flag)
{
	g1_send_vector("0x26_mic_on_head_up_off");
	expect_frame_padded("26 06 00 00 08 c9", 20);
	zassert_equal(g1_state()->head_up_mic_disabled, 1);
	g1_send_hex("3f 08");
	expect_frame_padded("3f 08 c9 01", 20);

	send_vector_with("0x26_mic_on_head_up_off", 5, 0x00);
	expect_frame_padded("26 06 00 00 08 c9", 20);
	zassert_equal(g1_state()->head_up_mic_disabled, 0);
	g1_send_hex("3f 08");
	expect_frame_padded("3f 08 c9 00", 20);
}

ZTEST(proto_session, test_0x26_unknown_sub_command_is_rejected)
{
	g1_send_hex("26 06 00 0b 00 01");
	expect_frame_padded("26 06 00 0b 00 ca", 20);

	g1_send_hex("26 06 00 0c 09 01");
	expect_frame_padded("26 06 00 0c 09 ca", 20);
	expect_no_frame();
}

/* length field 7, packet 6 */
ZTEST(proto_session, test_0x26_length_mismatch_is_rejected)
{
	g1_state()->head_up_mic_disabled = 0;

	g1_send_hex("26 07 00 0d 08 01");

	expect_frame_padded("26 06 00 0d 08 ca", 20);
	zassert_equal(g1_state()->head_up_mic_disabled, 0);
}

ZTEST(proto_session, test_0x3f_unknown_sub_command_is_rejected)
{
	g1_send_hex("3f 04");
	expect_frame_padded("3f 04 ca", 20);

	g1_send_hex("3f 09");
	expect_frame_padded("3f 09 ca", 20);
	expect_no_frame();
}
