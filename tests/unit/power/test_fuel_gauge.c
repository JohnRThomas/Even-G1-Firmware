/*
 * fuel_gauge.c on the fake charger, with the Arm-only nRF Fuel Gauge library faked, and the battery
 * model it hands to the library. Dyadic measurements (3.75 V, 0.125 A) keep the float arithmetic
 * exact.
 */
#include <math.h>

#include "harness.h"
#include "drivers.h"
#include "fakes.h"

#include "sensors_audio/fuel_gauge.c"

#define CHARGER G1_FAKE_CHARGER_DEV

FAKE_VALUE_FUNC(int, nrf_fuel_gauge_init, const struct nrf_fuel_gauge_init_parameters *, float *);
FAKE_VALUE_FUNC(float, nrf_fuel_gauge_process, float, float, float, float,
    struct nrf_fuel_gauge_state_info *);
FAKE_VALUE_FUNC(uint8_t, pt_comm_busy_get);
FAKE_VALUE_FUNC(int, lock_sensor_mutex);
FAKE_VALUE_FUNC(int, unlock_sensor_mutex);
uint8_t fuel_gauge_stuck; /* threads.c's flag; power does not build threads.c */

/* a copy: FFF keeps only the pointer to the caller's parameters */
static struct nrf_fuel_gauge_init_parameters init_params;

static int record_init_params(const struct nrf_fuel_gauge_init_parameters *params, float *v0)
{
  ARG_UNUSED(v0);
  init_params = *params;
  return nrf_fuel_gauge_init_fake.return_val;
}

static void fuel_gauge_init_before(void *fixture)
{
  ARG_UNUSED(fixture);
  RESET_FAKE(nrf_fuel_gauge_init);
  RESET_FAKE(nrf_fuel_gauge_process);
  RESET_FAKE(pt_comm_busy_get);
  RESET_FAKE(lock_sensor_mutex);
  RESET_FAKE(unlock_sensor_mutex);
  nrf_fuel_gauge_init_fake.custom_fake = record_init_params;
  get_glasses_name_fake.return_val = "S110LAAL290026";
  fuel_gauge_stuck = 0;
  fuel_gauge_ref_time = 0;
  fg_last_soc = 0;
  fg_last_soc_time = 0;
  zero_current_count = 0;
}

ZTEST_SUITE(fuel_gauge_init, NULL, NULL, fuel_gauge_init_before, NULL, NULL);

/* the fuel_gauge cases start with the gauge initialised on 3.75 V, 0.125 A, 25.5 C */
static void fuel_gauge_before(void *fixture)
{
  fuel_gauge_init_before(fixture);
  fake_charger_set(3750000, 125000, 25500);
  zassert_equal(fuel_gauge_init(CHARGER), 0);
}

ZTEST_SUITE(fuel_gauge, NULL, NULL, fuel_gauge_before, NULL, NULL);

static int update_with_soc(float soc)
{
  nrf_fuel_gauge_process_fake.return_val = soc;
  g1_clock_advance_ms(1000);
  return fuel_gauge_update(CHARGER);
}

ZTEST(fuel_gauge_init, test_init_starts_the_library_with_the_first_measurement_and_the_model)
{
  fake_charger_set(3750000, -125000, 25500);

  zassert_equal(fuel_gauge_init(CHARGER), 0);
  zassert_equal(nrf_fuel_gauge_init_fake.call_count, 1);
  zassert_equal(init_params.v0, 3.75f);
  zassert_equal(init_params.i0, -0.125f);
  zassert_equal(init_params.t0, 25.5f);
  zassert_not_null(init_params.model);
  expect_str(init_params.model->name, "EVENBAT");
  zassert_is_null(nrf_fuel_gauge_init_fake.arg1_val, "no adjusted-voltage output wanted");
  zassert_equal(fake_charger_fetch_count(), 1);
}

ZTEST(fuel_gauge_init, test_init_fails_with_the_charger_read_error_and_leaves_the_library_alone)
{
  fake_charger_fail_fetch(-EIO, 1);

  zassert_equal(fuel_gauge_init(CHARGER), -EIO);
  zassert_equal(nrf_fuel_gauge_init_fake.call_count, 0);
}

ZTEST(fuel_gauge_init, test_init_fails_with_the_library_error)
{
  fake_charger_set(3750000, 125000, 25500);
  nrf_fuel_gauge_init_fake.return_val = -EINVAL;

  zassert_equal(fuel_gauge_init(CHARGER), -EINVAL);
}

ZTEST(fuel_gauge, test_update_after_a_failed_init_does_not_run_the_library)
{
  fake_charger_set(3750000, 125000, 25500);
  nrf_fuel_gauge_init_fake.return_val = -EINVAL;
  zassert_equal(fuel_gauge_init(CHARGER), -EINVAL);
  g1_clock_advance_ms(1000);

  fuel_gauge_update(CHARGER);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0,
          "processed with t_delta %f s", (double)nrf_fuel_gauge_process_fake.arg3_val);
}

ZTEST(fuel_gauge, test_update_passes_voltage_current_temperature_and_elapsed_seconds)
{
  fake_charger_set(3875000, 62500, 30500);
  nrf_fuel_gauge_process_fake.return_val = 57.0f;
  g1_clock_advance_ms(2500);

  zassert_equal(fuel_gauge_update(CHARGER), 0);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 1);
  zassert_equal(nrf_fuel_gauge_process_fake.arg0_val, 3.875f, "voltage V");
  zassert_equal(nrf_fuel_gauge_process_fake.arg1_val, 0.0625f, "current A");
  zassert_equal(nrf_fuel_gauge_process_fake.arg2_val, 30.5f, "temperature C");
  zassert_equal(nrf_fuel_gauge_process_fake.arg3_val, 2.5f, "seconds since the init");
  zassert_is_null(nrf_fuel_gauge_process_fake.arg4_val);
}

ZTEST(fuel_gauge, test_update_measures_the_time_from_the_previous_update)
{
  nrf_fuel_gauge_process_fake.return_val = 57.0f;

  g1_clock_advance_ms(1500);
  zassert_equal(fuel_gauge_update(CHARGER), 0);
  g1_clock_advance_ms(1000);
  zassert_equal(fuel_gauge_update(CHARGER), 0);
  g1_clock_advance_ms(4000);
  zassert_equal(fuel_gauge_update(CHARGER), 0);

  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 3);
  zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[0], 1.5f);
  zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[1], 1.0f);
  zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[2], 4.0f);
}

ZTEST(fuel_gauge, test_update_less_than_1s_after_the_previous_one_returns_minus_1)
{
  g1_state()->battery_left = 42;
  g1_clock_advance_ms(999);

  zassert_equal(fuel_gauge_update(CHARGER), -1);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0);
  zassert_equal(g1_state()->battery_left, 42, "battery fields untouched");

  g1_clock_advance_ms(1);
  nrf_fuel_gauge_process_fake.return_val = 57.0f;
  zassert_equal(fuel_gauge_update(CHARGER), 0);
  zassert_equal(nrf_fuel_gauge_process_fake.arg3_val, 1.0f);
}

ZTEST(fuel_gauge, test_update_returns_the_charger_read_error)
{
  g1_state()->battery_left = 42;
  fake_charger_fail_fetch(-EIO, 1);
  g1_clock_advance_ms(1000);

  zassert_equal(fuel_gauge_update(CHARGER), -EIO);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0);
  zassert_equal(g1_state()->battery_left, 42);
}

ZTEST(fuel_gauge, test_update_while_the_factory_link_is_busy_measures_nothing)
{
  pt_comm_busy_get_fake.return_val = 1;
  g1_clock_advance_ms(1000);

  zassert_equal(fuel_gauge_update(CHARGER), 0);
  zassert_equal(fake_charger_fetch_count(), 1, "only the init's fetch");
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_update_reads_the_charger_under_the_sensor_mutex)
{
  update_with_soc(57.0f);

  zassert_equal(lock_sensor_mutex_fake.call_count, 1);
  zassert_equal(unlock_sensor_mutex_fake.call_count, 1);
}

/* the measurements are 5 ms apart: 5..6 ms per sleep on 1 ms ticks */
ZTEST(fuel_gauge, test_zero_current_is_measured_again_up_to_11_times)
{
  int64_t start;

  fake_charger_set(3750000, 0, 25500);
  nrf_fuel_gauge_process_fake.return_val = 50.0f;
  g1_clock_advance_ms(1000);
  start = g1_clock_uptime_ms();

  zassert_equal(fuel_gauge_update(CHARGER), 0);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 11);
  expect_elapsed_ms(start, 50, 60);
}

ZTEST(fuel_gauge, test_zero_current_retry_stops_at_the_first_nonzero_current)
{
  zassert_ok(fake_charger_queue(3750000, 0, 25500));
  zassert_ok(fake_charger_queue(3750000, 0, 25500));
  zassert_ok(fake_charger_queue(3750000, 125000, 25500));

  zassert_equal(update_with_soc(50.0f), 0);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 3);
  zassert_equal(nrf_fuel_gauge_process_fake.arg1_history[2], 0.125f);
}

ZTEST(fuel_gauge, test_zero_current_with_a_full_battery_is_not_measured_again)
{
  fake_charger_set(3750000, 0, 25500);

  update_with_soc(100.0f);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 1);
}

/* 1 s since the init, then the 5 ms sleep */
ZTEST(fuel_gauge, test_zero_current_retries_pass_the_time_since_the_previous_call)
{
  zassert_ok(fake_charger_queue(3750000, 0, 25500));
  zassert_ok(fake_charger_queue(3750000, 125000, 25500));

  zassert_equal(update_with_soc(50.0f), 0);
  zassert_equal(nrf_fuel_gauge_process_fake.call_count, 2);
  zassert_equal(nrf_fuel_gauge_process_fake.arg3_history[0], 1.0f);
  zassert_true(nrf_fuel_gauge_process_fake.arg3_history[1] >= 0.005f &&
           nrf_fuel_gauge_process_fake.arg3_history[1] <= 0.006f,
         "second t_delta %f s", (double)nrf_fuel_gauge_process_fake.arg3_history[1]);
}

ZTEST(fuel_gauge, test_soc_becomes_battery_left)
{
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
  } rows[] = {
    {92.75f, 92, 0}, {93.25f, 93, 0}, {93.5f, 93, 1}, {93.75f, 93, 1},
    {94.0f, 94, 0},  {94.25f, 94, 0}, {94.5f, 94, 1}, {94.75f, 94, 1},
    {95.0f, 95, 0},  {95.5f, 95, 0},
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    update_with_soc(rows[row].soc);
    zassert_equal(g1_state()->battery_left, rows[row].left, "soc %f",
            (double)rows[row].soc);
    zassert_equal(g1_state()->battery_soc_flag, rows[row].flag, "soc %f",
            (double)rows[row].soc);
  }
}

ZTEST(fuel_gauge, test_voltage_is_stored_in_10mv_steps_above_2v)
{
  fake_charger_set(3750000, 125000, 25500);
  update_with_soc(57.0f);
  zassert_equal(g1_state()->battery_voltage, 175, "3.75 V");

  fake_charger_set(4375000, 125000, 25500);
  update_with_soc(57.0f);
  zassert_equal(g1_state()->battery_voltage, 237, "4.375 V -> 437.5 - 200");
}

ZTEST(fuel_gauge, test_current_is_stored_in_ma_and_as_ma_plus_128)
{
  fake_charger_set(3750000, -125000, 25500);
  update_with_soc(57.0f);

  zassert_equal(g1_state()->battery_current_ma, -125);
  zassert_equal(g1_state()->battery_current, 3, "-125 mA + 128");

  fake_charger_set(3750000, 62500, 25500);
  update_with_soc(57.0f);
  zassert_equal(g1_state()->battery_current_ma, 62);
  zassert_equal(g1_state()->battery_current, 190, "62.5 + 128");
}

/* the charger ADC reads 0 A now and then */
ZTEST(fuel_gauge, test_zero_current_keeps_the_last_current_for_5_readings)
{
  fake_charger_set(3750000, -125000, 25500);
  update_with_soc(100.0f);
  zassert_equal(g1_state()->battery_current_ma, -125);

  fake_charger_set(3750000, 0, 25500);
  for (int reading = 1; reading <= 5; reading++) {
    update_with_soc(100.0f);
    zassert_equal(g1_state()->battery_current_ma, -125, "zero reading %d", reading);
  }
  update_with_soc(100.0f);
  zassert_equal(g1_state()->battery_current_ma, 0, "sixth zero reading");
  zassert_equal(g1_state()->battery_current, 128);
}

ZTEST(fuel_gauge, test_temperature_is_stored_in_degrees_and_centidegrees)
{
  fake_charger_set(3750000, 125000, 30500);
  update_with_soc(57.0f);

  zassert_equal(g1_state()->battery_temp, 30);
  zassert_equal(g1_state()->battery_temp_x100, 3050);
}

ZTEST(fuel_gauge, test_old_board_is_egp0x_or_egp10_to_egp13)
{
  static const struct {
    char *name;
    bool old;
  } rows[] = {
    {"S110LAAL290026", false}, {"S100demoglasses", false}, {"S110EGP0", true},
    {"S110EGP05", true},       {"S110EGP10", true},        {"S110EGP13", true},
    {"S110EGP14", false},      {"S110EGP19", false},       {"S110EGP20", false},
    {"S110EGQ05", false},
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    get_glasses_name_fake.return_val = rows[row].name;
    zassert_equal(is_old_board(), rows[row].old, "%s", rows[row].name);
  }
}

/* 4.2 V reaches 80 % of the model's 4.4 V charge */
ZTEST(fuel_gauge, test_old_board_soc_is_scaled_by_1_25_and_capped_at_100)
{
  get_glasses_name_fake.return_val = "S110EGP12";

  update_with_soc(60.0f);
  zassert_equal(g1_state()->battery_left, 75);
  update_with_soc(80.0f);
  zassert_equal(g1_state()->battery_left, 100);
  update_with_soc(90.0f);
  zassert_equal(g1_state()->battery_left, 100, "112.5 capped");
}

ZTEST(fuel_gauge, test_soc_unchanged_below_100_for_120s_marks_the_gauge_stuck)
{
  update_with_soc(50.0f); /* the 120 s start here */

  for (int seconds = 10; seconds < 120; seconds += 10) {
    g1_clock_advance_ms(10000);
    fuel_gauge_update(CHARGER);
    zassert_equal(fuel_gauge_stuck, 0, "after %d s", seconds);
  }
  g1_clock_advance_ms(10000);
  fuel_gauge_update(CHARGER);
  zassert_equal(fuel_gauge_stuck, 1, "after 120 s");
}

ZTEST(fuel_gauge, test_changing_soc_is_not_stuck)
{
  for (int seconds = 0; seconds < 200; seconds += 10) {
    nrf_fuel_gauge_process_fake.return_val = 50.0f + (float)(seconds / 10) * 0.25f;
    g1_clock_advance_ms(10000);
    fuel_gauge_update(CHARGER);
  }
  zassert_equal(fuel_gauge_stuck, 0);
}

ZTEST(fuel_gauge, test_full_battery_is_never_stuck)
{
  nrf_fuel_gauge_process_fake.return_val = 100.0f;
  for (int seconds = 0; seconds < 200; seconds += 10) {
    g1_clock_advance_ms(10000);
    fuel_gauge_update(CHARGER);
  }
  zassert_equal(fuel_gauge_stuck, 0);
}

ZTEST(fuel_gauge, test_battery_at_60c_enters_the_abnormal_temperature_mode_once)
{
  fake_charger_set(3750000, 125000, 60000);

  update_with_soc(57.0f);
  zassert_equal(g1_state()->display_mode, 0x0c);
  zassert_equal(report_event_to_phone_fake.call_count, 1);
  zassert_equal(sync_to_slave_fake.call_count, 1, "the master tells the slave");
  zassert_equal(sync_to_master_fake.call_count, 0);
  zassert_true(exit_running_task_fake.call_count >= 1);
  zassert_equal(exit_running_task_fake.arg1_val, 1, "last exit with notify");

  fake_charger_set(3750000, 125000, 65000);
  update_with_soc(57.0f);
  zassert_equal(report_event_to_phone_fake.call_count, 1, "not reported again");
  zassert_equal(sync_to_slave_fake.call_count, 1);
}

ZTEST(fuel_gauge, test_battery_below_60c_is_normal)
{
  fake_charger_set(3750000, 125000, 59500);

  update_with_soc(57.0f);
  zassert_not_equal(g1_state()->display_mode, 0x0c);
  zassert_equal(report_event_to_phone_fake.call_count, 0);
  zassert_equal(exit_running_task_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_battery_at_minus_20c_enters_the_abnormal_temperature_mode)
{
  fake_charger_set(3750000, 125000, -20000);

  update_with_soc(57.0f);
  zassert_equal(g1_state()->display_mode, 0x0c);
  zassert_equal(report_event_to_phone_fake.call_count, 1);
}

ZTEST(fuel_gauge, test_battery_above_minus_20c_is_normal)
{
  fake_charger_set(3750000, 125000, -19500);

  update_with_soc(57.0f);
  zassert_not_equal(g1_state()->display_mode, 0x0c);
  zassert_equal(report_event_to_phone_fake.call_count, 0);
}

ZTEST(fuel_gauge, test_slave_with_a_hot_battery_tells_the_master)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  fake_charger_set(3750000, 125000, 61000);

  update_with_soc(57.0f);
  zassert_equal(g1_state()->display_mode, 0x0c);
  zassert_equal(sync_to_master_fake.call_count, 1);
  zassert_equal(sync_to_slave_fake.call_count, 0);
  zassert_equal(report_event_to_phone_fake.call_count, 1);
}

/* battery_model.inc as text, to count its values: a short initializer would zero-fill silently */
__asm__(".pushsection .rodata.g1_battery_model_text,\"a\"\n"
  ".global g1_battery_model_text\n"
  "g1_battery_model_text:\n"
  ".incbin \"" G1_BATTERY_MODEL_INC "\"\n"
  ".byte 0\n"
  ".popsection\n");
extern const char g1_battery_model_text[];

ZTEST_SUITE(battery_model, NULL, NULL, NULL, NULL, NULL);

/* the number of values in the text's ".field = {...}" initializer; -1 if absent */
static int count_values(const char *field)
{
  char key[32];
  const char *cursor;
  const char *end;
  int count = 0;
  bool in_value = false;

  snprintf(key, sizeof(key), ".%s = {", field);
  cursor = strstr(g1_battery_model_text, key);
  end = cursor ? strchr(cursor, '}') : NULL;
  if (end == NULL) {
    return -1;
  }
  for (cursor += strlen(key); cursor < end; cursor++) {
    if (*cursor == ',') {
      in_value = false;
    } else if (*cursor != ' ' && *cursor != '\n' && *cursor != '\t' && !in_value) {
      in_value = true;
      count++;
    }
  }
  return count;
}

ZTEST(battery_model, test_every_table_has_the_length_the_library_expects)
{
  static const struct {
    const char *field;
    int len;
  } fields[] = {
    {"param_1", 201}, {"temps", 3},     {"param_2", 122}, {"param_3", 201},
    {"param_4", 201}, {"param_5", 201}, {"param_6", 201}, {"param_7", 122},
    {"param_8", 122}, {"param_9", 3},   {"param_10", 6},  {"param_11", 6},
    {"param_12", 3},
  };

  for (size_t row = 0; row < ARRAY_SIZE(fields); row++) {
    zassert_equal(count_values(fields[row].field), fields[row].len, "%s: %d values",
            fields[row].field, count_values(fields[row].field));
  }
}

ZTEST(battery_model, test_model_is_named_evenbat)
{
  zassert_equal(count_values("name"), 7);
  expect_str(battery_model.name, "EVENBAT");
}

ZTEST(battery_model, test_soc_grid_is_0_to_1_in_steps_of_0_005)
{
  for (int index = 0; index < 201; index++) {
    zassert_within(battery_model.param_1[index], index * 0.005f, 1e-6f,
             "param_1[%d] = %f", index, (double)battery_model.param_1[index]);
  }
}

/* 4.40 V: the charger's termination voltage */
ZTEST(battery_model, test_voltage_grid_rises_evenly_from_2_99_v_to_4_40_v)
{
  const float *volts = battery_model.param_2;
  const float step = (4.40f - 2.99f) / 121;

  expect_within(volts[0], 2.99f, 1e-6f);
  expect_within(volts[121], 4.40f, 1e-6f);
  for (int index = 1; index < 122; index++) {
    zassert_true(volts[index] > volts[index - 1], "param_2[%d]", index);
    zassert_within(volts[index] - volts[index - 1], step, 1e-5f, "step %d", index);
  }
}

ZTEST(battery_model, test_every_value_is_finite)
{
  const float *values = (const float *)&battery_model;
  size_t count = offsetof(struct battery_model, name) / sizeof(float);

  for (size_t index = 0; index < count; index++) {
    zassert_true(isfinite(values[index]), "value %zu", index);
  }
}
