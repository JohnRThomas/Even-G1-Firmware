/* Dyadic measurements (3.75 V, 0.125 A) keep the float arithmetic exact. */
#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "devices.h"
#include "fake_charger.h"
#include "fakes_power.h"

#include "sensors_audio/fuel_gauge.c"

#define CHARGER G1_FAKE_CHARGER_DEV

static void fuel_gauge_before(void *fixture)
{
	ARG_UNUSED(fixture);
	fuel_gauge_ref_time = 0;
	fg_last_soc = 0;
	fg_last_soc_time = 0;
	zero_current_count = 0;
}

ZTEST_SUITE(fuel_gauge, NULL, NULL, fuel_gauge_before, NULL, NULL);

/* uV, uA, milli-degrees C */
static void measure(int64_t v_uv, int64_t i_ua, int64_t t_mdegc)
{
	struct fake_charger_sample s = fake_charger_sample(v_uv, i_ua, t_mdegc);

	fake_charger_set_sample(&s);
}

static void queue_measure(int64_t v_uv, int64_t i_ua, int64_t t_mdegc)
{
	struct fake_charger_sample s = fake_charger_sample(v_uv, i_ua, t_mdegc);

	zassert_ok(fake_charger_queue_sample(&s));
}

static void init_gauge(void)
{
	measure(3750000, 125000, 25500);
	zassert_equal(fuel_gauge_init(CHARGER), 0);
	RESET_FAKE(nrf_fuel_gauge_process);
}

static int update_with_soc(float soc)
{
	nrf_fuel_gauge_process_fake.return_val = soc;
	g1_clock_advance_ms(1000);
	return fuel_gauge_update(CHARGER, 0);
}

ZTEST(fuel_gauge, test_init_starts_the_library_with_the_first_measurement_and_the_model)
{
	measure(3750000, -125000, 25500);

	zassert_equal(fuel_gauge_init(CHARGER), 0);
	zassert_equal(nrf_fuel_gauge_init_fake.call_count, 1);
	zassert_equal(g1_fg_init_params.v0, 3.75f);
	zassert_equal(g1_fg_init_params.i0, -0.125f);
	zassert_equal(g1_fg_init_params.t0, 25.5f);
	zassert_not_null(g1_fg_init_params.model);
	zassert_equal(strcmp(g1_fg_init_params.model->name, "EVENBAT"), 0);
	zassert_is_null(nrf_fuel_gauge_init_fake.arg1_val, "no adjusted-voltage output wanted");
	zassert_equal(fake_charger_fetch_count(), 1);
}

ZTEST(fuel_gauge, test_init_fails_with_the_charger_read_error_and_leaves_the_library_alone)
{
	fake_charger_fail_fetch(-EIO, 1);

	zassert_equal(fuel_gauge_init(CHARGER), -EIO);
	zassert_equal(nrf_fuel_gauge_init_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_init_fails_with_the_library_error)
{
	measure(3750000, 125000, 25500);
	nrf_fuel_gauge_init_fake.return_val = -EINVAL;

	zassert_equal(fuel_gauge_init(CHARGER), -EINVAL);
}

/* DEFECT: after a failed init the update still runs the uninitialised library. */
ZTEST_EXPECT_FAIL(fuel_gauge, test_update_after_a_failed_init_does_not_run_the_library);
ZTEST(fuel_gauge, test_update_after_a_failed_init_does_not_run_the_library)
{
	measure(3750000, 125000, 25500);
	nrf_fuel_gauge_init_fake.return_val = -EINVAL;
	zassert_equal(fuel_gauge_init(CHARGER), -EINVAL);
	g1_clock_advance_ms(1000);

	fuel_gauge_update(CHARGER, 0);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0,
		      "processed with t_delta %f s", (double)nrf_fuel_gauge_process_fake.arg3_val);
}

ZTEST(fuel_gauge, test_update_passes_voltage_current_temperature_and_elapsed_seconds)
{
	init_gauge();
	measure(3875000, 62500, 30500);
	nrf_fuel_gauge_process_fake.return_val = 57.0f;
	g1_clock_advance_ms(2500);

	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 1);
	zassert_equal(nrf_fuel_gauge_process_fake.arg0_val, 3.875f, "voltage V");
	zassert_equal(nrf_fuel_gauge_process_fake.arg1_val, 0.0625f, "current A");
	zassert_equal(nrf_fuel_gauge_process_fake.arg2_val, 30.5f, "temperature C");
	zassert_equal(nrf_fuel_gauge_process_fake.arg3_val, 2.5f, "seconds since the init");
	zassert_is_null(nrf_fuel_gauge_process_fake.arg4_val);
}

ZTEST(fuel_gauge, test_update_measures_the_time_from_the_previous_update)
{
	init_gauge();
	nrf_fuel_gauge_process_fake.return_val = 57.0f;

	g1_clock_advance_ms(1500);
	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	g1_clock_advance_ms(1000);
	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	g1_clock_advance_ms(4000);
	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);

	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 3);
	zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[0], 1.5f);
	zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[1], 1.0f);
	zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[2], 4.0f);
}

ZTEST(fuel_gauge, test_update_less_than_1s_after_the_previous_one_returns_minus_1)
{
	init_gauge();
	g1_state()->battery_left = 42;
	g1_clock_advance_ms(999);

	zassert_equal(fuel_gauge_update(CHARGER, 0), -1);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0);
	zassert_equal(g1_state()->battery_left, 42, "battery fields untouched");

	g1_clock_advance_ms(1);
	nrf_fuel_gauge_process_fake.return_val = 57.0f;
	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	zassert_equal(nrf_fuel_gauge_process_fake.arg3_val, 1.0f);
}

ZTEST(fuel_gauge, test_update_returns_the_charger_read_error)
{
	init_gauge();
	g1_state()->battery_left = 42;
	fake_charger_fail_fetch(-EIO, 1);
	g1_clock_advance_ms(1000);

	zassert_equal(fuel_gauge_update(CHARGER, 0), -EIO);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0);
	zassert_equal(g1_state()->battery_left, 42);
}

ZTEST(fuel_gauge, test_update_while_the_factory_link_is_busy_measures_nothing)
{
	init_gauge();
	pt_comm_busy_get_fake.return_val = 1;
	g1_clock_advance_ms(1000);

	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	zassert_equal(fake_charger_fetch_count(), 1, "only the init's fetch");
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_update_reads_the_charger_under_the_sensor_mutex)
{
	init_gauge();
	update_with_soc(57.0f);

	zassert_equal(lock_sensor_mutex_fake.call_count, 1);
	zassert_equal(unlock_sensor_mutex_fake.call_count, 1);
}

/* 5 ms apart */
ZTEST(fuel_gauge, test_zero_current_is_measured_again_up_to_11_times)
{
	int64_t t0;
	int64_t dt;

	init_gauge();
	measure(3750000, 0, 25500);
	nrf_fuel_gauge_process_fake.return_val = 50.0f;
	g1_clock_advance_ms(1000);
	t0 = g1_clock_uptime_ms();

	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	dt = g1_clock_uptime_ms() - t0;
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 11);
	/* 5..6 ms per sleep on 1 ms ticks */
	zassert_true(dt >= 50 && dt <= 60, "%lld ms", dt);
}

ZTEST(fuel_gauge, test_zero_current_retry_stops_at_the_first_nonzero_current)
{
	init_gauge();
	queue_measure(3750000, 0, 25500);
	queue_measure(3750000, 0, 25500);
	queue_measure(3750000, 125000, 25500);
	nrf_fuel_gauge_process_fake.return_val = 50.0f;
	g1_clock_advance_ms(1000);

	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 3);
	zassert_equal(nrf_fuel_gauge_process_fake.arg1_history[2], 0.125f);
}

ZTEST(fuel_gauge, test_zero_current_with_a_full_battery_is_not_measured_again)
{
	init_gauge();
	measure(3750000, 0, 25500);

	update_with_soc(100.0f);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 1);
}

/* DEFECT: every retry passes t_delta from the previous update, not the previous call. */
ZTEST_EXPECT_FAIL(fuel_gauge, test_zero_current_retries_pass_the_time_since_the_previous_call);
ZTEST(fuel_gauge, test_zero_current_retries_pass_the_time_since_the_previous_call)
{
	init_gauge();
	queue_measure(3750000, 0, 25500);
	queue_measure(3750000, 125000, 25500);
	nrf_fuel_gauge_process_fake.return_val = 50.0f;
	g1_clock_advance_ms(1000);

	zassert_equal(fuel_gauge_update(CHARGER, 0), 0);
	zassert_equal(nrf_fuel_gauge_process_fake.call_count, 2);
	zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[0], 1.0f);
	zassert_true(nrf_fuel_gauge_process_fake.arg3_history[1] >= 0.005f &&
			     nrf_fuel_gauge_process_fake.arg3_history[1] <= 0.006f,
		     "second t_delta %f s", (double)nrf_fuel_gauge_process_fake.arg3_history[1]);
}

ZTEST(fuel_gauge, test_soc_becomes_battery_left)
{
	init_gauge();

	update_with_soc(57.0f);
	zassert_equal(g1_state()->battery_left, 57);
	update_with_soc(100.0f);
	zassert_equal(g1_state()->battery_left, 100);
	update_with_soc(1.0f);
	zassert_equal(g1_state()->battery_left, 1);
}

/* ble_req.c reports 93 and 94 % in halves */
ZTEST(fuel_gauge, test_soc_flag_marks_the_upper_half_of_93_and_94_percent)
{
	static const struct {
		float soc;
		uint8_t left;
		uint8_t flag;
	} cases[] = {
		{92.75f, 92, 0}, {93.25f, 93, 0}, {93.5f, 93, 1}, {93.75f, 93, 1},
		{94.0f, 94, 0},  {94.25f, 94, 0}, {94.5f, 94, 1}, {94.75f, 94, 1},
		{95.0f, 95, 0},  {95.5f, 95, 0},
	};

	init_gauge();
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		update_with_soc(cases[i].soc);
		zassert_equal(g1_state()->battery_left, cases[i].left, "soc %f",
			      (double)cases[i].soc);
		zassert_equal(g1_state()->battery_soc_flag, cases[i].flag, "soc %f",
			      (double)cases[i].soc);
	}
}

ZTEST(fuel_gauge, test_voltage_is_stored_in_10mv_steps_above_2v)
{
	init_gauge();
	measure(3750000, 125000, 25500);
	update_with_soc(57.0f);
	zassert_equal(g1_state()->battery_voltage, 175, "3.75 V");

	measure(4375000, 125000, 25500);
	update_with_soc(57.0f);
	zassert_equal(g1_state()->battery_voltage, 237, "4.375 V -> 437.5 - 200");
}

ZTEST(fuel_gauge, test_current_is_stored_in_ma_and_as_ma_plus_128)
{
	init_gauge();
	measure(3750000, -125000, 25500);
	update_with_soc(57.0f);

	zassert_equal(g1_state()->battery_current_ma, -125);
	zassert_equal(g1_state()->battery_current, 3, "-125 mA + 128");

	measure(3750000, 62500, 25500);
	update_with_soc(57.0f);
	zassert_equal(g1_state()->battery_current_ma, 62);
	zassert_equal(g1_state()->battery_current, 190, "62.5 + 128");
}

/* the charger ADC reads 0 A now and then */
ZTEST(fuel_gauge, test_zero_current_keeps_the_last_current_for_5_readings)
{
	init_gauge();
	measure(3750000, -125000, 25500);
	update_with_soc(100.0f);
	zassert_equal(g1_state()->battery_current_ma, -125);

	measure(3750000, 0, 25500);
	for (int i = 1; i <= 5; i++) {
		update_with_soc(100.0f);
		zassert_equal(g1_state()->battery_current_ma, -125, "zero reading %d", i);
	}
	update_with_soc(100.0f);
	zassert_equal(g1_state()->battery_current_ma, 0, "sixth zero reading");
	zassert_equal(g1_state()->battery_current, 128);
}

ZTEST(fuel_gauge, test_temperature_is_stored_in_degrees_and_centidegrees)
{
	init_gauge();
	measure(3750000, 125000, 30500);
	update_with_soc(57.0f);

	zassert_equal(g1_state()->battery_temp, 30);
	zassert_equal(g1_state()->battery_temp_x100, 3050);
}

ZTEST(fuel_gauge, test_old_board_is_egp0x_or_egp10_to_egp13)
{
	static const struct {
		const char *name;
		bool old;
	} cases[] = {
		{"S110LAAL290026", false},
		{"S100demoglasses", false},
		{"S110EGP0", true},  {"S110EGP05", true}, {"S110EGP10", true},
		{"S110EGP13", true}, {"S110EGP14", false}, {"S110EGP19", false},
		{"S110EGP20", false}, {"S110EGQ05", false},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		memset(g1_power_glasses_name, 0, sizeof(g1_power_glasses_name));
		strcpy(g1_power_glasses_name, cases[i].name);
		zassert_equal(is_old_board(), cases[i].old, "%s", cases[i].name);
	}
}

/* 4.2 V reaches 80 % of the model's 4.4 V charge */
ZTEST(fuel_gauge, test_old_board_soc_is_scaled_by_1_25_and_capped_at_100)
{
	strcpy(g1_power_glasses_name, "S110EGP12");
	init_gauge();

	update_with_soc(60.0f);
	zassert_equal(g1_state()->battery_left, 75);
	update_with_soc(80.0f);
	zassert_equal(g1_state()->battery_left, 100);
	update_with_soc(90.0f);
	zassert_equal(g1_state()->battery_left, 100, "112.5 capped");
}

ZTEST(fuel_gauge, test_soc_unchanged_below_100_for_120s_marks_the_gauge_stuck)
{
	init_gauge();
	nrf_fuel_gauge_process_fake.return_val = 50.0f;
	g1_clock_advance_ms(1000);
	fuel_gauge_update(CHARGER, 0); /* the 120 s start here */

	for (int s = 10; s < 120; s += 10) {
		g1_clock_advance_ms(10000);
		fuel_gauge_update(CHARGER, 0);
		zassert_equal(fuel_gauge_stuck, 0, "after %d s", s);
	}
	g1_clock_advance_ms(10000);
	fuel_gauge_update(CHARGER, 0);
	zassert_equal(fuel_gauge_stuck, 1, "after 120 s");
}

ZTEST(fuel_gauge, test_changing_soc_is_not_stuck)
{
	init_gauge();
	for (int s = 0; s < 200; s += 10) {
		nrf_fuel_gauge_process_fake.return_val = 50.0f + (float)(s / 10) * 0.25f;
		g1_clock_advance_ms(10000);
		fuel_gauge_update(CHARGER, 0);
	}
	zassert_equal(fuel_gauge_stuck, 0);
}

ZTEST(fuel_gauge, test_full_battery_is_never_stuck)
{
	init_gauge();
	nrf_fuel_gauge_process_fake.return_val = 100.0f;
	for (int s = 0; s < 200; s += 10) {
		g1_clock_advance_ms(10000);
		fuel_gauge_update(CHARGER, 0);
	}
	zassert_equal(fuel_gauge_stuck, 0);
}

ZTEST(fuel_gauge, test_battery_at_60c_enters_the_abnormal_temperature_mode_once)
{
	init_gauge();
	measure(3750000, 125000, 60000);

	update_with_soc(57.0f);
	zassert_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(report_event_to_phone_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.call_count, 1, "the master tells the slave");
	zassert_equal(sync_to_master_fake.call_count, 0);
	zassert_true(exit_running_task_fake.call_count >= 1);
	zassert_equal(exit_running_task_fake.arg1_val, 1, "last exit with notify");

	measure(3750000, 125000, 65000);
	update_with_soc(57.0f);
	zassert_equal(report_event_to_phone_fake.call_count, 1, "not reported again");
	zassert_equal(sync_to_slave_fake.call_count, 1);
}

ZTEST(fuel_gauge, test_battery_below_60c_is_normal)
{
	init_gauge();
	measure(3750000, 125000, 59500);

	update_with_soc(57.0f);
	zassert_not_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(report_event_to_phone_fake.call_count, 0);
	zassert_equal(exit_running_task_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_battery_at_minus_20c_enters_the_abnormal_temperature_mode)
{
	init_gauge();
	measure(3750000, 125000, -20000);

	update_with_soc(57.0f);
	zassert_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(report_event_to_phone_fake.call_count, 1);
}

ZTEST(fuel_gauge, test_battery_above_minus_20c_is_normal)
{
	init_gauge();
	measure(3750000, 125000, -19500);

	update_with_soc(57.0f);
	zassert_not_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(report_event_to_phone_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_slave_with_a_hot_battery_tells_the_master)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	init_gauge();
	measure(3750000, 125000, 61000);

	update_with_soc(57.0f);
	zassert_equal(g1_state()->display_mode, 0x0c);
	zassert_equal(sync_to_master_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.call_count, 0);
	zassert_equal(report_event_to_phone_fake.call_count, 1);
}
