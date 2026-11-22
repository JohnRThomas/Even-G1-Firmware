/*
 * Z-Y-X Euler angles; at rest a = (-sin(theta), sin(phi) cos(theta), cos(phi) cos(theta)).
 * Mahony_getYaw() adds 180 degrees; Kp = 2, so small errors decay as exp(-2 t).
 */
#include <math.h>

#include "harness.h"

#define DEG2RAD(deg) ((deg) * 0.017453292519943295)

/* fusion thread period */
#define DT_THREAD 0.04f
#define DT_FINE   0.01f

/* degrees */
#define ANGLE_TOL 0.05f

static struct ahrs filter;

static void ahrs_before(void *fixture) {
  ARG_UNUSED(fixture);
  Mahony_init(&filter);
}

ZTEST_SUITE(ahrs, NULL, NULL, ahrs_before, NULL, NULL);

/* rates in rad/s */
static void run(float gx, float gy, float gz, float ax, float ay, float az, float dt, int steps) {
  for (int step = 0; step < steps; step++) {
    Mahony_updateIMU(gx, gy, gz, ax, ay, az, dt, &filter);
  }
}

static void gravity(double roll_deg, double pitch_deg, float *ax, float *ay, float *az) {
  double phi = DEG2RAD(roll_deg);
  double theta = DEG2RAD(pitch_deg);

  *ax = (float)-sin(theta);
  *ay = (float)(sin(phi) * cos(theta));
  *az = (float)(cos(phi) * cos(theta));
}

ZTEST(ahrs, test_level_sensor_converges_to_zero_roll_and_pitch) {
  filter.q0 = (float)cos(DEG2RAD(10.0));
  filter.q1 = (float)sin(DEG2RAD(10.0));
  expect_within(Mahony_getRoll(&filter), 20.0f, ANGLE_TOL);

  run(0, 0, 0, 0.0f, 0.0f, 1.0f, DT_THREAD, 250);

  expect_within(Mahony_getRoll(&filter), 0.0f, ANGLE_TOL);
  expect_within(Mahony_getPitch(&filter), 0.0f, ANGLE_TOL);
}

/* nose up is a negative rotation about +y */
ZTEST(ahrs, test_nose_up_30_deg_converges_to_pitch_minus_30) {
  float ax, ay, az;

  gravity(0.0, -30.0, &ax, &ay, &az);
  expect_within(ax, 0.5f, 1e-6f);

  run(0, 0, 0, ax, ay, az, DT_THREAD, 250);

  expect_within(Mahony_getPitch(&filter), -30.0f, ANGLE_TOL);
  expect_within(Mahony_getRoll(&filter), 0.0f, ANGLE_TOL);
}

ZTEST(ahrs, test_roll_90_deg_converges_to_roll_90) {
  run(0, 0, 0, 0.0f, 1.0f, 0.0f, DT_THREAD, 250);

  expect_within(Mahony_getRoll(&filter), 90.0f, ANGLE_TOL);
  expect_within(Mahony_getPitch(&filter), 0.0f, ANGLE_TOL);
}

/* gravity cannot move the heading */
ZTEST(ahrs, test_yaw_does_not_drift_while_still) {
  float ax, ay, az;

  gravity(0.0, -30.0, &ax, &ay, &az);
  run(0, 0, 0, ax, ay, az, DT_THREAD, 10000);

  expect_within(Mahony_getYaw(&filter), 180.0f, 0.01f);
  expect_within(Mahony_getPitch(&filter), -30.0f, ANGLE_TOL);
}

ZTEST(ahrs, test_yaw_integrates_z_rate) {
  run(0, 0, (float)DEG2RAD(90.0), 0.0f, 0.0f, 1.0f, DT_FINE, 100);

  expect_within(Mahony_getYaw(&filter), 270.0f, ANGLE_TOL);
  expect_within(Mahony_getRoll(&filter), 0.0f, ANGLE_TOL);
  expect_within(Mahony_getPitch(&filter), 0.0f, ANGLE_TOL);
}

/* two Newton steps on 0x5f3759df: ~4.7e-6 max (Lomont 2003) */
ZTEST(ahrs, test_invsqrt_relative_error_below_5e_6) {
  double worst = 0.0;
  double worst_input = 0.0;

  for (double input = 1e-6; input <= 1e6; input *= 1.0137) {
    double ref = 1.0 / sqrt((double)(float)input);
    double err = fabs((double)Mahony_invSqrt((float)input) / ref - 1.0);

    if (err > worst) {
      worst = err;
      worst_input = input;
    }
  }
  zassert_true(worst < 5e-6, "relative error %g at %g", worst, worst_input);
}

ZTEST(ahrs, test_invsqrt_of_exact_squares) {
  expect_within(Mahony_invSqrt(4.0f), 0.5f, 0.5f * 5e-6f);
  expect_within(Mahony_invSqrt(1.0f), 1.0f, 1.0f * 5e-6f);
  expect_within(Mahony_invSqrt(0.25f), 2.0f, 2.0f * 5e-6f);
  expect_within(Mahony_invSqrt(100.0f), 0.1f, 0.1f * 5e-6f);
}

ZTEST(ahrs, test_deltat_is_time_between_calls_in_seconds) {
  float dt;

  (void)Mahony_deltatUpdate(&filter);

  g1_clock_advance_ms(40);
  dt = Mahony_deltatUpdate(&filter);
  expect_within(dt, 0.040f, 1e-6f);
  zassert_equal(filter.deltat, dt, "deltat is stored in the state as well");

  g1_clock_advance_ms(1500);
  dt = Mahony_deltatUpdate(&filter);
  expect_within(dt, 1.5f, 1e-6f);
  zassert_equal(filter.lastUpdate, (uint32_t)g1_clock_uptime_ms());
}

ZTEST(ahrs, test_deltat_across_32_bit_ms_wrap) {
  uint32_t now = (uint32_t)g1_clock_uptime_ms();
  float dt;

  filter.lastUpdate = 0xfffffff6u;
  dt = Mahony_deltatUpdate(&filter);

  zassert_within(dt, (float)(now + 10u) / 1000.0f, 1e-6f, "got %f at uptime %u ms", (double)dt, now);
}

ZTEST(ahrs, test_first_period_after_init_is_time_since_init) {
  float dt;

  g1_clock_advance_ms(60000);
  Mahony_init(&filter);
  g1_clock_advance_ms(40);
  dt = Mahony_deltatUpdate(&filter);

  zassert_within(dt, 0.040f, 1e-6f, "got %f s at %lld ms uptime", (double)dt, g1_clock_uptime_ms());
}

/* 1 - exp(-Kp t) at 0.5 s: 0.632 (Kp 2), 0.221 (Kp 0.5) */
ZTEST(ahrs, test_default_kp_reaches_63_percent_of_small_step_in_half_a_second) {
  float ax, ay, az;

  gravity(1.0, 0.0, &ax, &ay, &az);
  run(0, 0, 0, ax, ay, az, DT_FINE, 50);

  expect_within(Mahony_getRoll(&filter), 0.632f, 0.01f);
}

ZTEST(ahrs, test_lower_kp_slows_convergence) {
  float ax, ay, az;

  filter.twoKp = 1.0f;
  gravity(1.0, 0.0, &ax, &ay, &az);
  run(0, 0, 0, ax, ay, az, DT_FINE, 50);

  expect_within(Mahony_getRoll(&filter), 0.221f, 0.01f);
}

/* Kp sin(err) = b: asin(0.025) = 1.4325 degrees */
ZTEST(ahrs, test_gyro_bias_without_ki_leaves_steady_tilt) {
  run(0.05f, 0, 0, 0.0f, 0.0f, 1.0f, DT_FINE, 3000);

  expect_within(Mahony_getRoll(&filter), 1.4325f, 0.01f);
  zassert_equal(filter.integralFBx, 0.0f);
}

/* slowest pole -0.29/s; 60 s is 17 time constants */
ZTEST(ahrs, test_ki_integral_cancels_gyro_bias) {
  filter.twoKi = 1.0f;
  run(0.05f, 0, 0, 0.0f, 0.0f, 1.0f, DT_FINE, 6000);

  expect_within(Mahony_getRoll(&filter), 0.0f, 0.01f);
  expect_within(filter.integralFBx, -0.05f, 0.0005f);
}

ZTEST(ahrs, test_zero_accelerometer_sample_skips_feedback) {
  filter.q0 = (float)cos(DEG2RAD(10.0));
  filter.q1 = (float)sin(DEG2RAD(10.0));
  filter.integralFBx = 0.1f;
  Mahony_updateIMU(0, 0, 0, 0.0f, 0.0f, 0.0f, DT_THREAD, &filter);

  zassert_false(isnan(filter.q0) || isnan(filter.q1) || isnan(filter.q2) || isnan(filter.q3));
  expect_within(Mahony_getRoll(&filter), 20.0f, ANGLE_TOL);
  zassert_equal(filter.integralFBx, 0.1f);
}

ZTEST(ahrs, test_quaternion_stays_normalised_over_10000_steps) {
  uint32_t seed = 12345;
  double worst = 0.0;

  filter.twoKi = 0.2f;
  for (int step = 0; step < 10000; step++) {
    float noise[6]; /* -1 .. 1 */
    double norm;

    for (int channel = 0; channel < 6; channel++) {
      seed = seed * 1103515245u + 12345u;
      noise[channel] = (float)((seed >> 8) & 0xffff) / 32768.0f - 1.0f;
    }
    Mahony_updateIMU(4.0f * noise[0], 4.0f * noise[1], 4.0f * noise[2], 0.3f + 0.5f * noise[3], -0.2f + 0.5f * noise[4],
                     0.9f + 0.3f * noise[5], DT_FINE, &filter);

    norm = sqrt((double)filter.q0 * filter.q0 + (double)filter.q1 * filter.q1 + (double)filter.q2 * filter.q2 +
                (double)filter.q3 * filter.q3);
    worst = MAX(fabs(norm - 1.0), worst);
  }
  zassert_true(worst < 1e-5, "worst |q| - 1 = %g", worst);
}

ZTEST(ahrs, test_init_is_level_heading_zero) {
  memset(&filter, 0x55, sizeof(filter));
  Mahony_init(&filter);

  zassert_equal(filter.q0, 1.0f);
  zassert_equal(filter.q1, 0.0f);
  zassert_equal(filter.q2, 0.0f);
  zassert_equal(filter.q3, 0.0f);
  zassert_equal(filter.twoKp, 4.0f);
  zassert_equal(filter.twoKi, 0.0f);
  expect_within(Mahony_getRoll(&filter), 0.0f, 1e-6f);
  expect_within(Mahony_getPitch(&filter), 0.0f, 1e-6f);
  expect_within(Mahony_getYaw(&filter), 180.0f, 1e-6f);
}
