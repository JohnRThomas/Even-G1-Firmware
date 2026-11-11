/*
 * Z-Y-X Euler angles; at rest a = (-sin(theta), sin(phi) cos(theta), cos(phi) cos(theta)).
 * Mahony_getYaw() adds 180 degrees; Kp = 2, so small errors decay as exp(-2 t).
 */
#include <math.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "ahrs/ahrs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD(d) ((d) * 0.017453292519943295)

/* fusion thread period */
#define DT_THREAD 0.04f
#define DT_FINE   0.01f

/* degrees */
#define ANGLE_TOL 0.05f

ZTEST_SUITE(ahrs, NULL, NULL, NULL, NULL, NULL);

/* rates in rad/s */
static void run(struct ahrs *a, float gx, float gy, float gz, float ax, float ay, float az,
		float dt, int n)
{
	for (int i = 0; i < n; i++) {
		Mahony_updateIMU(gx, gy, gz, ax, ay, az, dt, a);
	}
}

static void gravity(double roll_deg, double pitch_deg, float *ax, float *ay, float *az)
{
	double phi = DEG2RAD(roll_deg);
	double theta = DEG2RAD(pitch_deg);

	*ax = (float)-sin(theta);
	*ay = (float)(sin(phi) * cos(theta));
	*az = (float)(cos(phi) * cos(theta));
}

static double qnorm(const struct ahrs *a)
{
	return sqrt((double)a->q0 * a->q0 + (double)a->q1 * a->q1 + (double)a->q2 * a->q2 +
		    (double)a->q3 * a->q3);
}

ZTEST(ahrs, test_level_sensor_converges_to_zero_roll_and_pitch)
{
	struct ahrs a;

	Mahony_init(&a);
	a.q0 = (float)cos(DEG2RAD(10.0));
	a.q1 = (float)sin(DEG2RAD(10.0));
	zassert_within(Mahony_getRoll(&a), 20.0f, ANGLE_TOL, "start roll %f", (double)Mahony_getRoll(&a));

	run(&a, 0, 0, 0, 0.0f, 0.0f, 1.0f, DT_THREAD, 250);

	zassert_within(Mahony_getRoll(&a), 0.0f, ANGLE_TOL, "roll %f", (double)Mahony_getRoll(&a));
	zassert_within(Mahony_getPitch(&a), 0.0f, ANGLE_TOL, "pitch %f",
		       (double)Mahony_getPitch(&a));
}

/* nose up is a negative rotation about +y */
ZTEST(ahrs, test_nose_up_30_deg_converges_to_pitch_minus_30)
{
	struct ahrs a;
	float ax, ay, az;

	Mahony_init(&a);
	gravity(0.0, -30.0, &ax, &ay, &az);
	zassert_within(ax, 0.5f, 1e-6f, NULL);

	run(&a, 0, 0, 0, ax, ay, az, DT_THREAD, 250);

	zassert_within(Mahony_getPitch(&a), -30.0f, ANGLE_TOL, "pitch %f",
		       (double)Mahony_getPitch(&a));
	zassert_within(Mahony_getRoll(&a), 0.0f, ANGLE_TOL, "roll %f", (double)Mahony_getRoll(&a));
}

ZTEST(ahrs, test_roll_90_deg_converges_to_roll_90)
{
	struct ahrs a;

	Mahony_init(&a);
	run(&a, 0, 0, 0, 0.0f, 1.0f, 0.0f, DT_THREAD, 250);

	zassert_within(Mahony_getRoll(&a), 90.0f, ANGLE_TOL, "roll %f", (double)Mahony_getRoll(&a));
	zassert_within(Mahony_getPitch(&a), 0.0f, ANGLE_TOL, "pitch %f",
		       (double)Mahony_getPitch(&a));
}

/* gravity cannot move the heading */
ZTEST(ahrs, test_yaw_does_not_drift_while_still)
{
	struct ahrs a;
	float ax, ay, az;

	Mahony_init(&a);
	gravity(0.0, -30.0, &ax, &ay, &az);
	run(&a, 0, 0, 0, ax, ay, az, DT_THREAD, 10000);

	zassert_within(Mahony_getYaw(&a), 180.0f, 0.01f, "yaw %f", (double)Mahony_getYaw(&a));
	zassert_within(Mahony_getPitch(&a), -30.0f, ANGLE_TOL, "pitch %f",
		       (double)Mahony_getPitch(&a));
}

ZTEST(ahrs, test_yaw_integrates_z_rate)
{
	struct ahrs a;

	Mahony_init(&a);
	run(&a, 0, 0, (float)(M_PI / 2), 0.0f, 0.0f, 1.0f, DT_FINE, 100);

	zassert_within(Mahony_getYaw(&a), 270.0f, ANGLE_TOL, "yaw %f", (double)Mahony_getYaw(&a));
	zassert_within(Mahony_getRoll(&a), 0.0f, ANGLE_TOL, NULL);
	zassert_within(Mahony_getPitch(&a), 0.0f, ANGLE_TOL, NULL);
}

/* two Newton steps on 0x5f3759df: ~4.7e-6 max (Lomont 2003) */
ZTEST(ahrs, test_invsqrt_relative_error_below_5e_6)
{
	double worst = 0.0;
	double worst_x = 0.0;

	for (double x = 1e-6; x <= 1e6; x *= 1.0137) {
		double ref = 1.0 / sqrt((double)(float)x);
		double err = fabs((double)Mahony_invSqrt((float)x) / ref - 1.0);

		if (err > worst) {
			worst = err;
			worst_x = x;
		}
	}
	zassert_true(worst < 5e-6, "relative error %g at x = %g", worst, worst_x);
}

ZTEST(ahrs, test_invsqrt_of_exact_squares)
{
	zassert_within(Mahony_invSqrt(4.0f), 0.5f, 0.5f * 5e-6f, NULL);
	zassert_within(Mahony_invSqrt(1.0f), 1.0f, 1.0f * 5e-6f, NULL);
	zassert_within(Mahony_invSqrt(0.25f), 2.0f, 2.0f * 5e-6f, NULL);
	zassert_within(Mahony_invSqrt(100.0f), 0.1f, 0.1f * 5e-6f, NULL);
}

ZTEST(ahrs, test_deltat_is_time_between_calls_in_seconds)
{
	struct ahrs a;

	float dt;

	Mahony_init(&a);
	(void)Mahony_deltatUpdate(&a);

	g1_clock_advance_ms(40);
	dt = Mahony_deltatUpdate(&a);
	zassert_within(dt, 0.040f, 1e-6f, "got %f", (double)dt);
	zassert_equal(a.deltat, dt, "deltat is stored in the state as well");

	g1_clock_advance_ms(1500);
	dt = Mahony_deltatUpdate(&a);
	zassert_within(dt, 1.5f, 1e-6f, "got %f", (double)dt);
	zassert_equal(a.lastUpdate, (uint32_t)g1_clock_uptime_ms(), NULL);
}

ZTEST(ahrs, test_deltat_across_32_bit_ms_wrap)
{
	struct ahrs a;
	uint32_t now;
	float dt;

	Mahony_init(&a);
	a.lastUpdate = 0xfffffff6u;
	now = (uint32_t)g1_clock_uptime_ms();
	dt = Mahony_deltatUpdate(&a);

	zassert_within(dt, (float)(now + 10u) / 1000.0f, 1e-6f, "got %f at uptime %u ms", (double)dt,
		       now);
}

/* DEFECT: Mahony_init() sets lastUpdate = 0, so the first period is the whole uptime. */
ZTEST_EXPECT_FAIL(ahrs, test_first_period_after_init_is_time_since_init);
ZTEST(ahrs, test_first_period_after_init_is_time_since_init)
{
	struct ahrs a;
	float dt;

	g1_clock_advance_ms(60000);
	Mahony_init(&a);
	g1_clock_advance_ms(40);
	dt = Mahony_deltatUpdate(&a);

	zassert_within(dt, 0.040f, 1e-6f, "got %f s at %lld ms uptime", (double)dt,
		       g1_clock_uptime_ms());
}

/* 1 - exp(-Kp t) at 0.5 s: 0.632 (Kp 2), 0.221 (Kp 0.5) */
ZTEST(ahrs, test_default_kp_reaches_63_percent_of_small_step_in_half_a_second)
{
	struct ahrs a;
	float ax, ay, az;

	Mahony_init(&a);
	gravity(1.0, 0.0, &ax, &ay, &az);
	run(&a, 0, 0, 0, ax, ay, az, DT_FINE, 50);

	zassert_within(Mahony_getRoll(&a), 0.632f, 0.01f, "roll %f", (double)Mahony_getRoll(&a));
}

ZTEST(ahrs, test_lower_kp_slows_convergence)
{
	struct ahrs a;
	float ax, ay, az;

	Mahony_init(&a);
	a.twoKp = 1.0f;
	gravity(1.0, 0.0, &ax, &ay, &az);
	run(&a, 0, 0, 0, ax, ay, az, DT_FINE, 50);

	zassert_within(Mahony_getRoll(&a), 0.221f, 0.01f, "roll %f", (double)Mahony_getRoll(&a));
}

/* Kp sin(err) = b: asin(0.025) = 1.4325 degrees */
ZTEST(ahrs, test_gyro_bias_without_ki_leaves_steady_tilt)
{
	struct ahrs a;

	Mahony_init(&a);
	run(&a, 0.05f, 0, 0, 0.0f, 0.0f, 1.0f, DT_FINE, 3000);

	zassert_within(Mahony_getRoll(&a), 1.4325f, 0.01f, "roll %f", (double)Mahony_getRoll(&a));
	zassert_equal(a.integralFBx, 0.0f, NULL);
}

/* slowest pole -0.29/s; 60 s is 17 time constants */
ZTEST(ahrs, test_ki_integral_cancels_gyro_bias)
{
	struct ahrs a;

	Mahony_init(&a);
	a.twoKi = 1.0f;
	run(&a, 0.05f, 0, 0, 0.0f, 0.0f, 1.0f, DT_FINE, 6000);

	zassert_within(Mahony_getRoll(&a), 0.0f, 0.01f, "roll %f", (double)Mahony_getRoll(&a));
	zassert_within(a.integralFBx, -0.05f, 0.0005f, "integralFBx %f", (double)a.integralFBx);
}

ZTEST(ahrs, test_zero_accelerometer_sample_skips_feedback)
{
	struct ahrs a;

	Mahony_init(&a);
	a.q0 = (float)cos(DEG2RAD(10.0));
	a.q1 = (float)sin(DEG2RAD(10.0));
	a.integralFBx = 0.1f;
	Mahony_updateIMU(0, 0, 0, 0.0f, 0.0f, 0.0f, DT_THREAD, &a);

	zassert_false(isnan(a.q0) || isnan(a.q1) || isnan(a.q2) || isnan(a.q3), NULL);
	zassert_within(Mahony_getRoll(&a), 20.0f, ANGLE_TOL, "roll %f", (double)Mahony_getRoll(&a));
	zassert_equal(a.integralFBx, 0.1f, NULL);
}

ZTEST(ahrs, test_quaternion_stays_normalised_over_10000_steps)
{
	struct ahrs a;
	uint32_t seed = 12345;
	double worst = 0.0;

	Mahony_init(&a);
	a.twoKi = 0.2f;
	for (int i = 0; i < 10000; i++) {
		float r[6];

		for (int k = 0; k < 6; k++) {
			seed = seed * 1103515245u + 12345u;
			r[k] = (float)((seed >> 8) & 0xffff) / 32768.0f - 1.0f; /* -1 .. 1 */
		}
		Mahony_updateIMU(4.0f * r[0], 4.0f * r[1], 4.0f * r[2], 0.3f + 0.5f * r[3],
				 -0.2f + 0.5f * r[4], 0.9f + 0.3f * r[5], DT_FINE, &a);

		double err = fabs(qnorm(&a) - 1.0);

		if (err > worst) {
			worst = err;
		}
	}
	zassert_true(worst < 1e-5, "worst |q| - 1 = %g", worst);
}

ZTEST(ahrs, test_init_is_level_heading_zero)
{
	struct ahrs a;

	memset(&a, 0x55, sizeof(a));
	Mahony_init(&a);

	zassert_equal(a.q0, 1.0f, NULL);
	zassert_equal(a.q1, 0.0f, NULL);
	zassert_equal(a.q2, 0.0f, NULL);
	zassert_equal(a.q3, 0.0f, NULL);
	zassert_equal(a.twoKp, 4.0f, NULL);
	zassert_equal(a.twoKi, 0.0f, NULL);
	zassert_within(Mahony_getRoll(&a), 0.0f, 1e-6f, NULL);
	zassert_within(Mahony_getPitch(&a), 0.0f, 1e-6f, NULL);
	zassert_within(Mahony_getYaw(&a), 180.0f, 1e-6f, NULL);
}
