/* Control commands: 0x0E microphone, 0x10 head-up calibration. */
#include <zephyr/ztest.h>

#include "harness.h"
#include "session.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "fakes_misc.h"

ZTEST(proto_session, test_0x0e_mic_on_when_idle_starts_the_stream)
{
	is_idle_fake.return_val = true;

	g1_send_vector("0x0e_mic_on");

	expect_frame_padded("0e c9 01", 20);
	expect_no_frame();
	zassert_equal(request_dmic_stream_start_fake.call_count, 1);
}

/* tasks that use the microphone */
ZTEST(proto_session, test_0x0e_mic_on_during_a_microphone_feature_starts_the_stream)
{
	static const uint32_t tasks[] = {0x09, 0x0b, 0x0c, 0x10, 0x13, 0x14};

	is_idle_fake.return_val = false;
	g1_state()->dashboard_mode = 5;
	for (size_t i = 0; i < ARRAY_SIZE(tasks); i++) {
		g1_state()->task_status->task = tasks[i];
		g1_send_vector("0x0e_mic_on");
		expect_frame_padded("0e c9 01", 20);
		zassert_equal(request_dmic_stream_start_fake.call_count, i + 1, "task 0x%02x",
			      tasks[i]);
	}
}

/* dashboard_mode 6 = dashboard */
ZTEST(proto_session, test_0x0e_mic_on_on_the_dashboard_is_refused)
{
	is_idle_fake.return_val = false;
	g1_state()->dashboard_mode = 6;
	g1_state()->task_status->task = 0;

	g1_send_vector("0x0e_mic_on");

	expect_frame_padded("0e ca 01", 20);
	zassert_equal(request_dmic_stream_start_fake.call_count, 0);
}

ZTEST(proto_session, test_0x0e_mic_off_stops_the_stream)
{
	g1_send_vector("0x0e_mic_off");

	expect_frame_padded("0e c9 00", 20);
	expect_no_frame();
	zassert_equal(dmic_stream_stop_fake.call_count, 1);
}

/* imu.base = the user's horizontal pitch */
ZTEST(proto_session, test_0x10_sub_1_when_already_clear_does_not_notify)
{
	g1_send_vector("0x10_calibration_clear"); /* 10 05 00 01 01 */

	expect_frame_padded("10 06 00 01 01 c9", 20);
	zassert_false(dashboard_notified());
}

ZTEST(proto_session, test_0x10_sub_2_start_enters_calibration)
{
	g1_state()->imu.base = 1234;

	g1_send_vector("0x10_calibration_start");

	expect_frame_padded("10 06 00 0c 02 c9", 20);
	zassert_equal(g1_state()->horiz_pitch_cfg[0], 1);
	zassert_equal(g1_state()->horiz_pitch_cfg[1], 0);
	zassert_equal(g1_state()->imu.base, 1234);
	zassert_equal(g1_state()->esb_down_id, 0x0c);
	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal(update_persist_task_status_fake.arg1_val, 0x11);
	zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

ZTEST(proto_session, test_0x10_sub_3_is_rejected)
{
	g1_send_hex("10 05 00 07 03");

	expect_frame_padded("10 06 00 07 03 ca", 20);
	expect_no_frame();
}

/* length field 6, packet 5 */
ZTEST(proto_session, test_0x10_length_mismatch_is_rejected)
{
	g1_state()->imu.base = 1234;

	g1_send_hex("10 06 00 07 01");

	expect_frame_padded("10 06 00 07 01 ca", 20);
	zassert_equal(g1_state()->imu.base, 1234);
	zassert_false(dashboard_notified());
}
