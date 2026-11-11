/*
 * Pitch in 0.01 degree, nose up negative. lift = 50 x angle + offset(level byte).
 * Head-up window [reference - 4500 - lift, reference - lift] (reference = base, else ref),
 * else [lift - 9300, lift - 6300].
 */
#include "harness.h"
#include "drivers.h"
#include "fakes.h"

#include "imu/imu_fusion.c"

/* what imu_fusion.c uses outside the common fakes */
FAKE_VOID_FUNC(exit_temp_task, struct GlassesState *, int);
FAKE_VALUE_FUNC(uint8_t, is_persist_task_active, struct GlassesState *, int);
FAKE_VALUE_FUNC(int, get_work_mode);
atomic_t dashboard_lock_requested;
atomic_t dashboard_lock_reported;
uint8_t push_msg_syncing;
uint8_t screen_switch_busy;

/* level head at -10 degrees; angle 14h, level byte 1: lift = 1000 */
#define REF     (-1000)
#define WIN_TOP (-2000)
#define WIN_BOT (-6500)

static struct imu_ctx *imu;

static void imu_before(void *fixture)
{
  ARG_UNUSED(fixture);
  RESET_FAKE(exit_temp_task);
  atomic_clear(&dashboard_lock_requested);
  atomic_clear(&dashboard_lock_reported);
  screen_switch_busy = 0;
  imu_last_div = 300;
  imu_angle_threshold = 150;
  imu_last_wear = 3;
  memset(&imu_motion, 0, sizeof(imu_motion));
  imu_last_ref = 0;
  imu_last_base = 0;
  imu_last_ms = 0;
  imu_last_pitch = 0;
  imu_trigger_count = 0;
  imu_level_pitch = 0;
  imu_last_level = 0;

  imu = &g1_state()->imu;
  imu->div = 400;
  imu->level = 0x14;
  imu->wear = 1;
  imu->dof3_enable = 1;
}

/* imu_fusion_init() sets thread_running when it starts the fusion thread */
static void imu_after(void *fixture)
{
  ARG_UNUSED(fixture);
  if (imu->thread_running) {
    k_thread_abort(&imu_fusion_thread_data);
  }
}

ZTEST_SUITE(imu, NULL, NULL, imu_before, imu_after, NULL);

static int offset_of(int level_byte)
{
  return level_byte == 0 ? -350 : level_byte == 2 ? 700 : 0;
}

static void expect_thresholds_for_every_angle(int base, int ref)
{
  imu->base = base;
  imu->ref = ref;
  for (int level_byte = 0; level_byte <= 2; level_byte++) {
    for (int angle = 0x00; angle <= 0x3c; angle++) {
      int lift = 50 * angle + offset_of(level_byte);
      int reference = base ? base : ref;
      int top = reference - lift;
      int bot = reference - 4500 - lift;

      imu->level = angle;
      imu->wear = level_byte;
      update_head_up_thresholds(imu);
      zassert_equal(imu->thr_low, top,
              "angle %02x level byte %d: top %d, expected %d", angle,
              level_byte, imu->thr_low, top);
      zassert_equal(imu->thr_high, bot,
              "angle %02x level byte %d: bottom %d, expected %d", angle,
              level_byte, imu->thr_high, bot);
    }
  }
}

ZTEST(imu, test_thresholds_for_every_angle_and_level_byte_from_the_factory_reference)
{
  expect_thresholds_for_every_angle(0, REF);

  /* 3Ch, level byte 2: lift = 3700 */
  zassert_equal(imu->thr_low, -4700);
  zassert_equal(imu->thr_high, -9200);
}

ZTEST(imu, test_thresholds_use_the_user_horizontal_from_0x10)
{
  expect_thresholds_for_every_angle(-2500, REF);

  imu->level = 0x14;
  imu->wear = 1;
  update_head_up_thresholds(imu);
  zassert_equal(imu->thr_low, -3500);
  zassert_equal(imu->thr_high, -8000);
}

ZTEST(imu, test_thresholds_without_any_reference_at_angle_0)
{
  imu->level = 0;
  imu->wear = 1;
  update_head_up_thresholds(imu);
  zassert_equal(imu->thr_low, -6300);
  zassert_equal(imu->thr_high, -9300);
}

/*
 * DEFECT: without a reference thr_low is 50 x angle + offset - 6300, so a larger angle needs
 * the head raised less.
 */
ZTEST_EXPECT_FAIL(imu, test_without_a_reference_a_larger_angle_needs_more_lift);
ZTEST(imu, test_without_a_reference_a_larger_angle_needs_more_lift)
{
  int prev;

  imu->wear = 1;
  imu->level = 0;
  update_head_up_thresholds(imu);
  prev = imu->thr_low;
  for (int angle = 0x01; angle <= 0x3c; angle++) {
    imu->level = angle;
    update_head_up_thresholds(imu);
    zassert_true(imu->thr_low < prev, "angle %02x: top %d, angle %02x: top %d",
           angle - 1, prev, angle, imu->thr_low);
    prev = imu->thr_low;
  }
}

static void pitch(int pitch_x100)
{
  float angles[3] = { 180.0f, pitch_x100 / 100.0f, 0.0f };

  imu_head_gesture(imu, pitch_x100, 18000, 0, angles);
}

static void gesture_setup(void)
{
  imu->ref = REF;
  imu->mode = 1;
  update_head_up_thresholds(imu);
  zassert_equal(imu->thr_low, WIN_TOP);
  zassert_equal(imu->thr_high, WIN_BOT);
  pitch(REF);
  pitch(REF);
}

/* gesture_setup(), then a head lift that reports head up */
static void start_with_head_up(void)
{
  gesture_setup();
  pitch(-3000);
  pitch(-3100);
  zassert_equal(imu->mode, 2, "no head up");
  pitch(-3100);
}

static void expect_events(int count)
{
  zassert_equal(report_event_to_phone_fake.call_count, count, "%d events to the phone",
          report_event_to_phone_fake.call_count);
}

static void expect_event(int index, int cmd)
{
  zassert_equal(report_event_to_phone_fake.arg0_history[index], cmd, "event %d: F5 %02x",
          index, report_event_to_phone_fake.arg0_history[index]);
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
  zassert_equal(imu->mode, 2);
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
  zassert_equal(imu->mode, 1);
}

ZTEST(imu, test_head_still_past_the_threshold_is_not_head_up)
{
  gesture_setup();

  pitch(-3000);
  for (int pitch_x100 = -3020; pitch_x100 >= -3200; pitch_x100 -= 20) {
    pitch(pitch_x100);
  }
  expect_events(0);
  zassert_equal(imu->mode, 1);
}

ZTEST(imu, test_user_horizontal_from_0x10_moves_the_head_up_window)
{
  imu->base = -2500; /* window [-8000, -3500] */
  imu->ref = REF;
  imu->mode = 1;
  update_head_up_thresholds(imu);
  zassert_equal(imu->thr_low, -3500);
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
  start_with_head_up();

  for (int sample = 0; sample < 20; sample++) {
    pitch(sample % 2 ? -3300 : -3500);
  }
  expect_events(1);
  zassert_equal(imu->mode, 2);
}

ZTEST(imu, test_looking_down_again_reports_head_down)
{
  g1_state()->panel.panel_brightness_level = 7;
  start_with_head_up();

  pitch(-1850);
  expect_events(1);
  pitch(-1790);
  expect_events(2);
  expect_event(1, 3);
  zassert_equal(imu->mode, 1);
  zassert_equal(sync_to_slave_fake.call_count, 1);
  zassert_equal(sync_to_slave_fake.arg1_val, 1);
  zassert_equal(g1_state()->esb_arg2, 7);
  zassert_equal(exit_temp_task_fake.call_count, 1);
  zassert_equal(exit_temp_task_fake.arg1_val, 0);
}

ZTEST(imu, test_head_down_needs_one_degree_past_the_threshold)
{
  start_with_head_up();

  pitch(-1960);
  pitch(-1905);
  pitch(-1960);
  pitch(-1900); /* = top + 100: not past it */
  expect_events(1);
  zassert_equal(imu->mode, 2);

  pitch(-1850);
  pitch(-1790);
  expect_events(2);
  expect_event(1, 3);
}

ZTEST(imu, test_looking_far_beyond_the_window_counts_as_head_down)
{
  start_with_head_up();

  pitch(-7000);
  pitch(-7100);
  expect_events(2);
  expect_event(1, 3);
  zassert_equal(imu->mode, 1);
}

ZTEST(imu, test_head_down_on_the_wait_screen_syncs_8)
{
  start_with_head_up();
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
  zassert_equal(imu->mode, 2);
  zassert_false(atomic_test_bit(&dashboard_head_up_opened, 1));
}

ZTEST(imu, test_screen_13_puts_the_angles_into_the_esb_packet)
{
  struct imu_esb *esb = (struct imu_esb *)&g1_state()->esb_block;
  float angles[3] = { 123.5f, -12.25f, 4.0f };

  gesture_setup();
  g1_state()->sync.screen_id = 13;
  imu_head_gesture(imu, -1225, 12350, 400, angles);

  zassert_equal(esb->tx_cmd, 12);
  zassert_equal(esb->tx_arg, 0);
  zassert_mem_equal(esb->tx_buf, angles, sizeof(angles));
  zassert_equal((int32_t)imu_pitch_x100, -1225);
}

static void expect_lock_message(int count)
{
  zassert_equal(g1_esb_response_log.count, count, "%d responses queued",
          g1_esb_response_log.count);
  for (int index = 0; index < count; index++) {
    expect_pkt(&g1_esb_response_log, index, "50 06 00 00 01 01");
  }
  zassert_equal(send_response_data_to_ble_fake.call_count, count);
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
  zassert_equal(imu->mode, 1);
  zassert_false(atomic_test_bit(&dashboard_head_up_opened, 1));
  expect_lock_message(1);
}

ZTEST(imu, test_lock_answer_waits_until_the_head_up_dashboard_closed)
{
  start_with_head_up();
  atomic_set_bit(&dashboard_lock_requested, 1);

  pitch(-3100);
  pitch(-3100);
  expect_lock_message(0);

  atomic_clear_bit(&dashboard_head_up_opened, 1);
  pitch(-3100);
  expect_lock_message(1);
}

static void expect_odr(int index, enum sensor_channel chan, int32_t hz)
{
  struct fake_imu_attr_call call;

  zassert_true(index < fake_imu_attr_count(), "attr_set %d missing", index);
  call = fake_imu_attr(index);
  zassert_equal(call.chan, chan, "attr_set %d: channel %d", index, call.chan);
  zassert_equal(call.attr, SENSOR_ATTR_SAMPLING_FREQUENCY, "attr_set %d: attribute %d",
          index, call.attr);
  zassert_equal(call.val.val1, hz, "attr_set %d: %d Hz, expected %d", index, call.val.val1,
          hz);
  zassert_equal(call.val.val2, 0);
}

/* LSM6DSO ODR steps, datasheet Table 44 */
ZTEST(imu, test_update_imu_mode_sets_the_accelerometer_rate_per_mode)
{
  zassert_equal(update_imu_mode(imu, 1), 0);
  zassert_equal(update_imu_mode(imu, 2), 0);
  zassert_equal(update_imu_mode(imu, 0), 0);

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
  zassert_equal(update_imu_mode(imu, 1), 0);
  zassert_equal(fake_imu_attr_count(), 0);
}

ZTEST(imu, test_update_imu_mode_while_initialised_is_refused)
{
  imu->inited = 1;
  zassert_equal(update_imu_mode(imu, 1), -1);
  zassert_equal(fake_imu_attr_count(), 0);
}

/* k_sleep(5 ms) lasts 6 ms on 1 ms ticks */
ZTEST(imu, test_update_imu_mode_retries_a_failed_rate_every_5_ms)
{
  int64_t start = g1_clock_uptime_ms();

  fake_imu_fail_attr(SENSOR_CHAN_ACCEL_XYZ, -EIO, 3);
  zassert_equal(update_imu_mode(imu, 1), 0);
  expect_elapsed_ms(start, 15, 18);
  zassert_equal(fake_imu_attr_count(), 5);
  zassert_equal(fake_imu_odr(SENSOR_CHAN_ACCEL_XYZ), 12);
}

ZTEST(imu, test_update_imu_mode_gives_up_after_20_tries)
{
  fake_imu_fail_attr(SENSOR_CHAN_ACCEL_XYZ, -EIO, -1);
  zassert_equal(update_imu_mode(imu, 1), -3);
  zassert_equal(fake_imu_attr_count(), 20);
  zassert_equal(fake_imu_odr(SENSOR_CHAN_GYRO_XYZ), -1, "gyroscope touched");

  fake_imu_fail_attr(SENSOR_CHAN_GYRO_XYZ, -EIO, -1);
  zassert_equal(update_imu_mode(imu, 2), -4);
  zassert_equal(fake_imu_attr_count(), 20 + 1 + 20);
  zassert_equal(fake_imu_odr(SENSOR_CHAN_ACCEL_XYZ), 26);
}

ZTEST(imu, test_set_cb_stores_the_sample_period_without_touching_the_sensor)
{
  zassert_equal(register_imu_funsion_context(imu), 0);
  zassert_equal_ptr(imu->init_cb, imu_fusion_init);
  zassert_not_null(imu->set_cb);

  imu->set_cb(imu, 80);
  zassert_equal(imu->sample_period_ms, 80);
  imu->set_cb(imu, 38);
  zassert_equal(imu->sample_period_ms, 38);
  zassert_equal(fake_imu_attr_count(), 0);
}

ZTEST(imu, test_init_on_the_master_starts_the_fusion_at_52_hz)
{
  imu->ref = REF;
  zassert_equal(imu_fusion_init(imu), 0);

  zassert_equal(fake_imu_attr_count(), 2);
  expect_odr(0, SENSOR_CHAN_ACCEL_XYZ, 52);
  expect_odr(1, SENSOR_CHAN_GYRO_XYZ, 0);
  zassert_equal(imu->thread_running, 1);
  zassert_equal(imu->sample_period_ms, 0);
  zassert_equal(imu->thr_low, WIN_TOP);
  zassert_equal(imu->thr_high, WIN_BOT);
  zassert_equal_ptr(imu->dev, G1_FAKE_IMU_DEV);
}

ZTEST(imu, test_init_on_the_slave_keeps_the_sensor_powered_down)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  zassert_equal(imu_fusion_init(imu), 0);
  expect_odr(0, SENSOR_CHAN_ACCEL_XYZ, 0);
  expect_odr(1, SENSOR_CHAN_GYRO_XYZ, 0);
}

ZTEST(imu, test_init_on_a_device_that_is_not_ready_fails_without_a_thread)
{
  g1_device_set_ready(G1_FAKE_IMU_DEV, false);

  zassert_equal(imu_fusion_init(imu), -3);
  zassert_equal(fake_imu_attr_count(), 1);
  zassert_equal(imu->thread_running, 0);
  g1_clock_advance_ms(500);
  zassert_equal(fake_imu_fetch_count(), 0);
}

ZTEST(imu, test_init_with_a_failing_gyroscope_fails_without_a_thread)
{
  fake_imu_fail_attr(SENSOR_CHAN_GYRO_XYZ, -EIO, -1);

  zassert_equal(imu_fusion_init(imu), -4);
  zassert_equal(imu->thread_running, 0);
  g1_clock_advance_ms(500);
  zassert_equal(fake_imu_fetch_count(), 0);
}

/* gyroscope 0, as powered down */
static void head_at(double pitch_deg)
{
  double rad = pitch_deg * 0.017453292519943295;

  fake_imu_set(-FAKE_IMU_G * sin(rad), 0.0, FAKE_IMU_G * cos(rad), 0.0, 0.0, 0.0);
}

/* Fixed gyro offsets give a steady 0.93 degree pitch offset, inside the tolerance. */
ZTEST(imu, test_thread_turns_a_head_lift_into_the_same_events_as_the_direct_calls)
{
  imu->ref = REF;
  head_at(-10.0);
  zassert_equal(imu_fusion_init(imu), 0);

  g1_clock_advance_ms(4000);
  expect_events(0);
  zassert_true(fake_imu_fetch_count() > 200, "%d samples", fake_imu_fetch_count());
  zassert_equal(fake_imu_get_count(), 2 * fake_imu_fetch_count());
  expect_within((int32_t)imu_pitch_x100, REF, 150);
  zassert_equal(imu->mode, 1);

  head_at(-40.0);
  g1_clock_advance_ms(2000);
  expect_events(1);
  expect_event(0, 2);
  zassert_equal(imu->mode, 2);
  zassert_true(atomic_test_bit(&dashboard_head_up_opened, 1));
  expect_within((int32_t)imu_pitch_x100, -4000, 150);

  head_at(-10.0);
  g1_clock_advance_ms(3000);
  expect_events(2);
  expect_event(1, 3);
  zassert_equal(imu->mode, 1);
  zassert_equal(sync_to_slave_fake.call_count, 1);
  zassert_equal(sync_to_slave_fake.arg1_val, 1);
  zassert_equal(exit_temp_task_fake.call_count, 1);
}

ZTEST(imu, test_thread_skips_failed_samples)
{
  imu->ref = REF;
  head_at(-10.0);
  fake_imu_fail_fetch(-EIO, -1);
  zassert_equal(imu_fusion_init(imu), 0);

  g1_clock_advance_ms(1000);
  zassert_true(fake_imu_fetch_count() >= 40, "%d tries", fake_imu_fetch_count());
  zassert_equal(fake_imu_get_count(), 0);
  expect_events(0);
}
