/*
 * Pitch in 0.01 degree, nose up negative. t = 50 x angle + offset(level byte).
 * Head-up window [r - 4500 - t, r - t] (r = base, else ref), else [t - 9300, t - 6300].
 */
#include <math.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "devices.h"
#include "fake_imu.h"
#include "fakes_esb.h"
#include "fakes_fusion.h"
#include "fakes_ipc.h"
#include "fakes_misc.h"

#include "imu/imu_fusion.c"

#define DEG2RAD(d) ((d) * 0.017453292519943295)

/* level head at -10 degrees; angle 14h, level byte 1: t = 1000 */
#define REF       (-1000)
#define WIN_TOP   (-2000)
#define WIN_BOT   (-6500)

static struct imu_ctx *c;
static bool thread_started;

static void imu_before(void *fixture)
{
	ARG_UNUSED(fixture);
	imu_last_div = 300;
	imu_angle_threshold = 150;
	imu_last_wear = 3;
	memset(&imu_motion, 0, sizeof(imu_motion));
	imu_pitch_x100 = 0;
	imu_yaw_x100 = 0;
	imu_roll_x100 = 0;
	atomic_clear(&dashboard_head_up_opened);
	imu_last_ref = 0;
	imu_last_base = 0;
	imu_last_ms = 0;
	imu_last_pitch = 0;
	imu_trigger_count = 0;
	imu_level_pitch = 0;
	imu_last_level = 0;
	thread_started = false;

	c = &g1_state()->imu;
	c->div = 400;
	c->level = 0x14;
	c->wear = 1;
	c->dof3_enable = 1;
	c->base = 0;
	c->ref = 0;
}

static void imu_after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (thread_started) {
		k_thread_abort(&imu_fusion_thread_data);
		thread_started = false;
	}
}

ZTEST_SUITE(imu, NULL, NULL, imu_before, imu_after, NULL);

static int offset_of(int level_byte)
{
	return level_byte == 0 ? -350 : level_byte == 2 ? 700 : 0;
}

static void check_all_angles(int base, int ref)
{
	c->base = base;
	c->ref = ref;
	for (int lb = 0; lb <= 2; lb++) {
		for (int angle = 0x00; angle <= 0x3c; angle++) {
			int t = 50 * angle + offset_of(lb);
			int r = base ? base : ref;
			int top = r - t;
			int bot = r - 4500 - t;

			c->level = angle;
			c->wear = lb;
			update_head_up_thresholds(c);
			zassert_equal(c->thr_low, top, "angle %02x level byte %d: top %d, expected %d",
				      angle, lb, c->thr_low, top);
			zassert_equal(c->thr_high, bot,
				      "angle %02x level byte %d: bottom %d, expected %d", angle, lb,
				      c->thr_high, bot);
		}
	}
}

ZTEST(imu, test_thresholds_for_every_angle_and_level_byte_from_the_factory_reference)
{
	check_all_angles(0, REF);

	/* 3Ch, level byte 2: t = 3700 */
	zassert_equal(c->thr_low, -4700);
	zassert_equal(c->thr_high, -9200);
}

ZTEST(imu, test_thresholds_use_the_user_horizontal_from_0x10)
{
	check_all_angles(-2500, REF);

	c->level = 0x14;
	c->wear = 1;
	update_head_up_thresholds(c);
	zassert_equal(c->thr_low, -3500);
	zassert_equal(c->thr_high, -8000);
}

ZTEST(imu, test_thresholds_without_any_reference_at_angle_0)
{
	c->level = 0;
	c->wear = 1;
	update_head_up_thresholds(c);
	zassert_equal(c->thr_low, -6300);
	zassert_equal(c->thr_high, -9300);
}

/* DEFECT: without a reference the top is t - 6300, so a larger angle needs less lift. */
ZTEST_EXPECT_FAIL(imu, test_without_a_reference_a_larger_angle_needs_more_lift);
ZTEST(imu, test_without_a_reference_a_larger_angle_needs_more_lift)
{
	int prev;

	c->wear = 1;
	c->level = 0;
	update_head_up_thresholds(c);
	prev = c->thr_low;
	for (int angle = 0x01; angle <= 0x3c; angle++) {
		c->level = angle;
		update_head_up_thresholds(c);
		zassert_true(c->thr_low < prev, "angle %02x: top %d, angle %02x: top %d", angle - 1,
			     prev, angle, c->thr_low);
		prev = c->thr_low;
	}
}

static void pitch(int p)
{
	float angles[3] = { 180.0f, p / 100.0f, 0.0f };

	imu_head_gesture(c, p, 18000, 0, angles);
}

static void gesture_setup(void)
{
	c->ref = REF;
	c->mode = 1;
	update_head_up_thresholds(c);
	zassert_equal(c->thr_low, WIN_TOP);
	zassert_equal(c->thr_high, WIN_BOT);
	pitch(REF);
	pitch(REF);
}

static void head_up(void)
{
	pitch(-3000);
	pitch(-3100);
	zassert_equal(c->mode, 2, "no head up");
	pitch(-3100);
}

static void expect_events(int n)
{
	zassert_equal(report_event_to_phone_fake.call_count, n, "%d events to the phone",
		      report_event_to_phone_fake.call_count);
}

static void expect_event(int i, int cmd)
{
	zassert_equal(report_event_to_phone_fake.arg0_history[i], cmd, "event %d: F5 %02x", i,
		      report_event_to_phone_fake.arg0_history[i]);
}

/* each sample must move at least 0.5 degree */
ZTEST(imu, test_looking_up_past_the_threshold_reports_head_up)
{
	gesture_setup();

	pitch(-3000);
	expect_events(0);
	pitch(-3100);
	expect_events(1);
	expect_event(0, 2);
	zassert_equal(c->mode, 2);
	zassert_true(atomic_test_bit(&dashboard_head_up_opened, 1));
	zassert_equal(screen_switch_busy, 0);
}

ZTEST(imu, test_one_sample_past_the_threshold_is_not_head_up)
{
	gesture_setup();

	pitch(-3000);
	pitch(REF);
	pitch(REF);
	expect_events(0);
	zassert_equal(c->mode, 1);
}

ZTEST(imu, test_head_still_past_the_threshold_is_not_head_up)
{
	gesture_setup();

	pitch(-3000);
	for (int p = -3020; p >= -3200; p -= 20) {
		pitch(p);
	}
	expect_events(0);
	zassert_equal(c->mode, 1);
}

ZTEST(imu, test_user_horizontal_from_0x10_moves_the_head_up_window)
{
	c->base = -2500; /* window [-8000, -3500] */
	c->ref = REF;
	c->mode = 1;
	update_head_up_thresholds(c);
	zassert_equal(c->thr_low, -3500);
	pitch(-2500);
	pitch(-2500);

	pitch(-3000); /* inside the factory window only */
	pitch(-3100);
	expect_events(0);
	pitch(-3600);
	pitch(-3700);
	expect_events(1);
	expect_event(0, 2);
}

ZTEST(imu, test_no_repeat_while_the_head_stays_up)
{
	gesture_setup();
	head_up();

	for (int i = 0; i < 20; i++) {
		pitch(i % 2 ? -3300 : -3500);
	}
	expect_events(1);
	zassert_equal(c->mode, 2);
}

ZTEST(imu, test_looking_down_again_reports_head_down)
{
	gesture_setup();
	g1_state()->panel.panel_brightness_level = 7;
	head_up();

	pitch(-1850);
	expect_events(1);
	pitch(-1790);
	expect_events(2);
	expect_event(1, 3);
	zassert_equal(c->mode, 1);
	zassert_equal(sync_to_slave_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.arg1_val, 1);
	zassert_equal(g1_state()->esb_arg2, 7);
	zassert_equal(exit_temp_task_fake.call_count, 1);
	zassert_equal(exit_temp_task_fake.arg1_val, 0);
}

ZTEST(imu, test_head_down_needs_one_degree_past_the_threshold)
{
	gesture_setup();
	head_up();

	pitch(-1960);
	pitch(-1905);
	pitch(-1960);
	pitch(-1900); /* = top + 100: not past it */
	expect_events(1);
	zassert_equal(c->mode, 2);

	pitch(-1850);
	pitch(-1790);
	expect_events(2);
	expect_event(1, 3);
}

ZTEST(imu, test_looking_far_beyond_the_window_counts_as_head_down)
{
	gesture_setup();
	head_up();

	pitch(-7000);
	pitch(-7100);
	expect_events(2);
	expect_event(1, 3);
	zassert_equal(c->mode, 1);
}

ZTEST(imu, test_head_down_on_the_wait_screen_syncs_8)
{
	gesture_setup();
	head_up();
	g1_state()->sync.screen_id = 1;

	pitch(-1850);
	pitch(-1790);
	expect_event(1, 3);
	zassert_equal(sync_to_slave_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.arg1_val, 8);
}

/* 9 = teleprompter */
ZTEST(imu, test_head_up_on_an_app_screen_does_not_open_the_dashboard)
{
	gesture_setup();
	g1_state()->sync.screen_id = 9;

	pitch(-3000);
	pitch(-3100);
	expect_events(1);
	expect_event(0, 2);
	zassert_equal(c->mode, 2);
	zassert_false(atomic_test_bit(&dashboard_head_up_opened, 1));
}

ZTEST(imu, test_screen_13_puts_the_angles_into_the_esb_packet)
{
	struct imu_esb *esb = (struct imu_esb *)&g1_state()->esb_block;
	float angles[3] = { 123.5f, -12.25f, 4.0f };
	float sent[3];

	gesture_setup();
	g1_state()->sync.screen_id = 13;
	imu_head_gesture(c, -1225, 12350, 400, angles);

	zassert_equal(esb->tx_cmd, 12);
	zassert_equal(esb->tx_arg, 0);
	memcpy(sent, esb->tx_buf, sizeof(sent));
	zassert_mem_equal(sent, angles, sizeof(angles));
	zassert_equal((int32_t)imu_pitch_x100, -1225);
}

static void expect_lock_message(int n)
{
	zassert_equal(g1_esb_response_log.count, n, "%d responses queued",
		      g1_esb_response_log.count);
	for (int i = 0; i < n; i++) {
		static const uint8_t want[6] = { 0x50, 0x06, 0x00, 0x00, 0x01, 0x01 };

		zassert_equal(g1_esb_response_log.len[i], 6);
		zassert_mem_equal(g1_esb_response_log.data[i], want, 6);
	}
	zassert_equal(send_response_data_to_ble_fake.call_count, n);
}

ZTEST(imu, test_locked_dashboard_answers_once_and_stays_closed_on_head_up)
{
	atomic_set_bit(&dashboard_lock_requested, 1);
	gesture_setup();
	expect_lock_message(1);
	zassert_true(atomic_test_bit(&dashboard_lock_reported, 1));

	pitch(-3000);
	pitch(-3100);
	pitch(-3100);
	expect_events(1);
	expect_event(0, 2);
	zassert_equal(c->mode, 1);
	zassert_false(atomic_test_bit(&dashboard_head_up_opened, 1));
	expect_lock_message(1);
}

ZTEST(imu, test_lock_answer_waits_until_the_head_up_dashboard_closed)
{
	gesture_setup();
	head_up();
	atomic_set_bit(&dashboard_lock_requested, 1);

	pitch(-3100);
	pitch(-3100);
	expect_lock_message(0);

	atomic_clear_bit(&dashboard_head_up_opened, 1);
	pitch(-3100);
	expect_lock_message(1);
}

static void expect_odr(int i, enum sensor_channel chan, int32_t hz)
{
	struct fake_imu_attr_call a;

	zassert_true(i < fake_imu_attr_count(), "attr_set %d missing", i);
	a = fake_imu_attr(i);
	zassert_equal(a.chan, chan, "attr_set %d: channel %d", i, a.chan);
	zassert_equal(a.attr, SENSOR_ATTR_SAMPLING_FREQUENCY, "attr_set %d: attribute %d", i,
		      a.attr);
	zassert_equal(a.val.val1, hz, "attr_set %d: %d Hz, expected %d", i, a.val.val1, hz);
	zassert_equal(a.val.val2, 0);
}

/* LSM6DSO ODR steps, datasheet Table 44 */
ZTEST(imu, test_update_imu_mode_sets_the_accelerometer_rate_per_mode)
{
	zassert_equal(update_imu_mode(c, 1), 0);
	zassert_equal(update_imu_mode(c, 2), 0);
	zassert_equal(update_imu_mode(c, 0), 0);

	zassert_equal(fake_imu_attr_count(), 6);
	expect_odr(0, SENSOR_CHAN_ACCEL_XYZ, 12);
	expect_odr(1, SENSOR_CHAN_GYRO_XYZ, 0);
	expect_odr(2, SENSOR_CHAN_ACCEL_XYZ, 26);
	expect_odr(3, SENSOR_CHAN_GYRO_XYZ, 0);
	expect_odr(4, SENSOR_CHAN_ACCEL_XYZ, 0);
	expect_odr(5, SENSOR_CHAN_GYRO_XYZ, 0);
}

ZTEST(imu, test_update_imu_mode_on_the_slave_touches_nothing)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	zassert_equal(update_imu_mode(c, 1), 0);
	zassert_equal(fake_imu_attr_count(), 0);
}

ZTEST(imu, test_update_imu_mode_while_initialised_is_refused)
{
	c->inited = 1;
	zassert_equal(update_imu_mode(c, 1), -1);
	zassert_equal(fake_imu_attr_count(), 0);
}

/* k_sleep(5 ms) lasts 6 ms on 1 ms ticks */
ZTEST(imu, test_update_imu_mode_retries_a_failed_rate_every_5_ms)
{
	int64_t t;

	fake_imu_fail_attr(SENSOR_CHAN_ACCEL_XYZ, -EIO, 3);
	t = k_uptime_get();
	zassert_equal(update_imu_mode(c, 1), 0);
	t = k_uptime_get() - t;
	zassert_true(t >= 15 && t <= 18, "%lld ms", t);
	zassert_equal(fake_imu_attr_count(), 5);
	zassert_equal(fake_imu_odr(SENSOR_CHAN_ACCEL_XYZ), 12);
}

ZTEST(imu, test_update_imu_mode_gives_up_after_20_tries)
{
	fake_imu_fail_attr(SENSOR_CHAN_ACCEL_XYZ, -EIO, -1);
	zassert_equal(update_imu_mode(c, 1), -3);
	zassert_equal(fake_imu_attr_count(), 20);
	zassert_equal(fake_imu_odr(SENSOR_CHAN_GYRO_XYZ), -1, "gyroscope touched");

	fake_imu_fail_attr(SENSOR_CHAN_GYRO_XYZ, -EIO, -1);
	zassert_equal(update_imu_mode(c, 2), -4);
	zassert_equal(fake_imu_attr_count(), 20 + 1 + 20);
	zassert_equal(fake_imu_odr(SENSOR_CHAN_ACCEL_XYZ), 26);
}

ZTEST(imu, test_set_cb_stores_the_sample_period_without_touching_the_sensor)
{
	zassert_equal(register_imu_funsion_context(c), 0);
	zassert_equal_ptr(c->init_cb, imu_fusion_init);
	zassert_not_null(c->set_cb);

	c->set_cb(c, 80);
	zassert_equal(c->sample_period_ms, 80);
	c->set_cb(c, 38);
	zassert_equal(c->sample_period_ms, 38);
	zassert_equal(fake_imu_attr_count(), 0);
}

static int init(void)
{
	int ret = imu_fusion_init(c);

	thread_started = ret == 0;
	return ret;
}

ZTEST(imu, test_init_on_the_master_starts_the_fusion_at_52_hz)
{
	c->ref = REF;
	zassert_equal(init(), 0);

	zassert_equal(fake_imu_attr_count(), 2);
	expect_odr(0, SENSOR_CHAN_ACCEL_XYZ, 52);
	expect_odr(1, SENSOR_CHAN_GYRO_XYZ, 0);
	zassert_equal(c->thread_running, 1);
	zassert_equal(c->sample_period_ms, 0);
	zassert_equal(c->thr_low, WIN_TOP);
	zassert_equal(c->thr_high, WIN_BOT);
	zassert_equal_ptr(c->dev, G1_FAKE_IMU_DEV);
}

ZTEST(imu, test_init_on_the_slave_keeps_the_sensor_powered_down)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	zassert_equal(init(), 0);
	expect_odr(0, SENSOR_CHAN_ACCEL_XYZ, 0);
	expect_odr(1, SENSOR_CHAN_GYRO_XYZ, 0);
}

ZTEST(imu, test_init_on_a_device_that_is_not_ready_fails_without_a_thread)
{
	g1_device_set_ready(G1_FAKE_IMU_DEV, false);

	zassert_equal(init(), -3);
	zassert_equal(fake_imu_attr_count(), 1);
	zassert_equal(c->thread_running, 0);
	g1_clock_advance_ms(500);
	zassert_equal(fake_imu_fetch_count(), 0);
}

ZTEST(imu, test_init_with_a_failing_gyroscope_fails_without_a_thread)
{
	fake_imu_fail_attr(SENSOR_CHAN_GYRO_XYZ, -EIO, -1);

	zassert_equal(init(), -4);
	zassert_equal(c->thread_running, 0);
	g1_clock_advance_ms(500);
	zassert_equal(fake_imu_fetch_count(), 0);
}

/* gyroscope 0, as powered down */
static void head_at(double pitch_deg)
{
	double th = DEG2RAD(pitch_deg);
	struct fake_imu_sample s = fake_imu_sample(-FAKE_IMU_G * sin(th), 0.0,
						   FAKE_IMU_G * cos(th), 0.0, 0.0, 0.0);

	fake_imu_set_sample(&s);
}

/* Fixed gyro offsets give a steady 0.93 degree pitch offset, inside the tolerance. */
ZTEST(imu, test_thread_turns_a_head_lift_into_the_same_events_as_the_direct_calls)
{
	c->ref = REF;
	head_at(-10.0);
	zassert_equal(init(), 0);

	g1_clock_advance_ms(4000);
	expect_events(0);
	zassert_true(fake_imu_fetch_count() > 200, "%d samples", fake_imu_fetch_count());
	zassert_equal(fake_imu_get_count(), 2 * fake_imu_fetch_count());
	zassert_within((int32_t)imu_pitch_x100, REF, 150, "pitch %d", (int32_t)imu_pitch_x100);
	zassert_equal(c->mode, 1);

	head_at(-40.0);
	g1_clock_advance_ms(2000);
	expect_events(1);
	expect_event(0, 2);
	zassert_equal(c->mode, 2);
	zassert_true(atomic_test_bit(&dashboard_head_up_opened, 1));
	zassert_within((int32_t)imu_pitch_x100, -4000, 150, "pitch %d", (int32_t)imu_pitch_x100);

	head_at(-10.0);
	g1_clock_advance_ms(3000);
	expect_events(2);
	expect_event(1, 3);
	zassert_equal(c->mode, 1);
	zassert_equal(sync_to_slave_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.arg1_val, 1);
	zassert_equal(exit_temp_task_fake.call_count, 1);
}

ZTEST(imu, test_thread_skips_failed_samples)
{
	c->ref = REF;
	head_at(-10.0);
	fake_imu_fail_fetch(-EIO, -1);
	zassert_equal(init(), 0);

	g1_clock_advance_ms(1000);
	zassert_true(fake_imu_fetch_count() >= 40, "%d tries", fake_imu_fetch_count());
	zassert_equal(fake_imu_get_count(), 0);
	expect_events(0);
}
