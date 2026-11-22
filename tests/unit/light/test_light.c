/* OPT3007 datasheet (SBOS864). Readings are R << E, in 0.01 lux. */
#include "drivers.h"
#include "opt3007_emul.h"

/* for its statics */
#include "sensors_audio/opt3007.c"

/* C810h with M[1:0] = 11b */
#define CONTINUOUS_MODE_CONFIG 0xce10

static void light_before(void *fixture) {
  ARG_UNUSED(fixture);
  opt3007_ctx = NULL;
  opt3007_ready = false;
  als_level = 0;
}

ZTEST_SUITE(light, NULL, NULL, light_before, NULL, NULL);

static struct opt3007_context *ctx(void) {
  return &g1_state()->opt3007_context;
}

static void init_and_settle(void) {
  zassert_equal(opt3007_chip_init(ctx()), 0);
  g1_clock_advance_ms(OPT3007_EMUL_CONV_800_MS + OPT3007_EMUL_AUTO_FIRST_MS);
}

/* any 1000 ms window ends an 800 ms conversion */
static void next_result(uint16_t raw) {
  opt3007_emul_set_result(raw);
  g1_clock_advance_ms(1000);
}

/* IDs per datasheet 7.6.1.1.5/6 */
ZTEST(light, test_chip_init_accepts_ti_ids_and_writes_continuous_mode) {
  zassert_equal(opt3007_chip_init(ctx()), 0);

  zassert_equal(opt3007_emul.writes, 1);
  zassert_equal(opt3007_emul.write_reg[0], OPT3007_EMUL_REG_CONFIG);
  zassert_equal(opt3007_emul.write_val[0], CONTINUOUS_MODE_CONFIG, "config written 0x%04x", opt3007_emul.write_val[0]);
  zassert_equal(ctx()->ready, 1);
  zassert_true(opt3007_ready);
}

ZTEST(light, test_chip_init_without_context_marks_the_glasses_state_context_ready) {
  zassert_equal(opt3007_chip_init(NULL), 0);
  zassert_equal(g1_state()->opt3007_context.ready, 1);
}

ZTEST(light, test_chip_init_rejects_a_wrong_manufacturer_id) {
  opt3007_emul.mfg_id = 0x1234;

  zassert_equal(opt3007_chip_init(ctx()), -ENOTSUP);
  zassert_equal(opt3007_emul.writes, 0);
  zassert_equal(ctx()->ready, 0);
  zassert_equal(opt3007_read(0), -1);
}

ZTEST(light, test_chip_init_rejects_a_wrong_device_id) {
  opt3007_emul.device_id = 0x3002;

  zassert_equal(opt3007_chip_init(ctx()), -ENOTSUP);
  zassert_equal(opt3007_emul.writes, 0);
  zassert_equal(ctx()->ready, 0);
}

ZTEST(light, test_chip_init_on_a_nak_returns_eio) {
  opt3007_emul.nak = true;

  zassert_equal(opt3007_chip_init(ctx()), -EIO);
  zassert_equal(ctx()->ready, 0);
  zassert_false(opt3007_ready);
}

ZTEST(light, test_reg_read_on_a_bus_that_is_not_ready_returns_enodev_without_a_transfer) {
  uint16_t value = 0;

  g1_device_set_ready(G1_I2C1, false);

  zassert_equal(opt3001_reg_read(OPT3007_EMUL_REG_LOW_LIMIT, &value), -ENODEV);
  zassert_equal(opt3007_emul.transfers, 0);
}

ZTEST(light, test_reg_write_on_a_nak_returns_the_bus_error) {
  opt3007_emul.nak = true;

  zassert_equal(opt3001_reg_write(OPT3007_EMUL_REG_HIGH_LIMIT, 0xa5c3), -EIO);
  zassert_equal(opt3007_emul.high_limit, 0xbfff);
}

ZTEST(light, test_reg_write_on_a_bus_that_is_not_ready_returns_enodev) {
  g1_device_set_ready(G1_I2C1, false);

  zassert_equal(opt3001_reg_write(OPT3007_EMUL_REG_HIGH_LIMIT, 0xa5c3), -ENODEV);
  zassert_equal(opt3007_emul.transfers, 0);
}

ZTEST(light, test_reg_update_changes_only_the_masked_bits) {
  opt3007_emul.low_limit = 0xf0f0;

  zassert_equal(opt3001_reg_update(OPT3007_EMUL_REG_LOW_LIMIT, 0x00ff, 0x1234), 0);
  zassert_equal(opt3007_emul.low_limit, 0xf034, "0x%04x", opt3007_emul.low_limit);
}

ZTEST(light, test_reg_update_with_a_failed_read_writes_nothing_and_returns_eio) {
  opt3007_emul.nak = true;

  zassert_equal(opt3001_reg_update(OPT3007_EMUL_REG_LOW_LIMIT, 0x00ff, 0x1234), -EIO);
  zassert_equal(opt3007_emul.writes, 0);
}

ZTEST(light, test_read_before_init_returns_minus_one_without_a_transfer) {
  zassert_equal(opt3007_read(0), -1);
  zassert_equal(opt3007_emul.transfers, 0);
}

/* datasheet Table 4 */
ZTEST(light, test_read_decodes_the_datasheet_table_4_examples_in_hundredths_of_lux) {
  static const struct {
    uint16_t result;
    int reading;
  } rows[] = {
    {0x0001, 1},      {0x0fff, 4095},   {0x3456, 8880},   {0x789a, 281856}, {0x8800, 524288},
    {0x9400, 524288}, {0xa200, 524288}, {0xb100, 524288}, {0xb001, 2048},   {0xbfff, 8386560},
  };

  init_and_settle();
  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    next_result(rows[row].result);
    zassert_equal(opt3007_read(0), rows[row].reading, "result 0x%04x: read %d, want %d", rows[row].result,
                  opt3007_read(0), rows[row].reading);
  }
}

ZTEST(light, test_read_on_a_nak_after_init_returns_minus_one) {
  init_and_settle();
  opt3007_emul.nak = true;

  zassert_equal(opt3007_read(0), -1);
}

ZTEST(light, test_deinit_makes_reads_return_minus_one) {
  init_and_settle();

  opt3007_chip_deinit();

  zassert_equal(ctx()->ready, 0);
  zassert_equal(opt3007_read(0), -1);
}

ZTEST(light, test_register_context_installs_init_and_read_callbacks) {
  register_opt3007_context(ctx());

  zassert_equal_ptr(ctx()->init_cb, opt3007_chip_init);
  zassert_equal_ptr(ctx()->read_cb, opt3007_read);
}

/* band edges of the piecewise map, by hand */
ZTEST(light, test_lux_to_level_at_every_band_boundary) {
  static const struct {
    int reading;
    uint8_t level;
  } rows[] = {
    {0, 0},     {4, 0},     {5, 1},      {49, 9},     {50, 10},    {59, 10},      {60, 11},
    {199, 24},  {200, 25},  {249, 25},   {250, 26},   {999, 40},   {1000, 41},    {1999, 41},
    {2000, 42}, {9999, 42}, {10000, 42}, {79999, 42}, {80000, 42}, {INT_MAX, 42},
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    zassert_equal(als_lux_to_level(rows[row].reading), rows[row].level, "reading %d: level %u, want %u",
                  rows[row].reading, als_lux_to_level(rows[row].reading), rows[row].level);
  }
}

ZTEST(light, test_brightness_level_is_0xff_without_a_usable_reading) {
  zassert_equal(opt3007_get_brightness_level(ctx()), 0xff);
  /* first conversion not done */
  zassert_equal(opt3007_chip_init(ctx()), 0);
  zassert_equal(opt3007_get_brightness_level(ctx()), 0xff);
}

/* uncalibrated: (120 - 50) / 10 + 10 = 17 */
ZTEST(light, test_brightness_level_uses_the_lux_table_up_to_curve_60000) {
  static const int uncalibrated[] = {0, -1, 60000};

  init_and_settle();
  next_result(0x0078); /* E 0, R 120 */
  for (size_t row = 0; row < ARRAY_SIZE(uncalibrated); row++) {
    g1_state()->luminance_curve = uncalibrated[row];
    zassert_equal(opt3007_get_brightness_level(ctx()), 17, "curve %d", uncalibrated[row]);
  }

  g1_state()->luminance_curve = 60001;
  zassert_equal(opt3007_get_brightness_level(ctx()), 3);
}

/* curve 1e6: scaled light = reading */
ZTEST(light, test_calibrated_brightness_level_moves_one_step_per_reading) {
  static const uint8_t up[] = {3, 6, 12, 19, 26, 42, 42};
  static const uint8_t down[] = {26, 19, 12, 6, 3, 0, 0};

  init_and_settle();
  g1_state()->luminance_curve = 1000000;

  next_result(0x07d0);
  for (size_t step = 0; step < ARRAY_SIZE(up); step++) {
    zassert_equal(opt3007_get_brightness_level(ctx()), up[step], "up step %u", step);
  }
  next_result(0x0001);
  for (size_t step = 0; step < ARRAY_SIZE(down); step++) {
    zassert_equal(opt3007_get_brightness_level(ctx()), down[step], "down step %u", step);
  }
}

/* level 3 holds for 1.6..6.0, level 6 down to 4.0 */
ZTEST(light, test_calibrated_brightness_level_holds_inside_the_hysteresis_band) {
  static const struct {
    uint16_t reading;
    uint8_t level;
  } seq[] = {
    {3, 3}, {5, 3}, {6, 3}, {7, 6}, {4, 6}, {3, 3},
  };

  init_and_settle();
  g1_state()->luminance_curve = 1000000;
  for (size_t row = 0; row < ARRAY_SIZE(seq); row++) {
    next_result(seq[row].reading);
    zassert_equal(opt3007_get_brightness_level(ctx()), seq[row].level, "reading %u", seq[row].reading);
  }
}

ZTEST(light, test_max_lux_is_minus_one_before_init) {
  zassert_equal(opt3007_get_max_lux(), -1);
}

ZTEST(light, test_max_lux_passes_through_zero_and_read_errors) {
  g1_state()->luminance_curve = 277000;
  zassert_equal(opt3007_chip_init(ctx()), 0);
  zassert_equal(opt3007_get_max_lux(), 0);

  g1_clock_advance_ms(1000);
  opt3007_emul.nak = true;
  zassert_equal(opt3007_get_max_lux(), -1);
}

/* reading x curve / 1e6, truncated */
ZTEST(light, test_max_lux_scales_the_reading_by_the_calibration_curve) {
  init_and_settle();

  g1_state()->luminance_curve = 277000;
  next_result(0x789a);
  zassert_equal(opt3007_get_max_lux(), 78074);

  g1_state()->luminance_curve = 1500000;
  next_result(0x0fff);
  zassert_equal(opt3007_get_max_lux(), 6142);
}

/* the ALS is on the panel's LDSW1 rail */
ZTEST(light, test_enable_power_for_panel_switches_ldsw1_on_and_inits_the_sensor) {
  enable_power_for_panel();

  zassert_equal(mfd_npm1300_reg_write_fake.call_count, 1);
  zassert_equal_ptr(mfd_npm1300_reg_write_fake.arg0_val, G1_NPM1300_DEV);
  zassert_equal(mfd_npm1300_reg_write_fake.arg1_val, G1_NPM1300_LDSW_BASE);
  zassert_equal(mfd_npm1300_reg_write_fake.arg2_val, G1_NPM1300_LDSW1_EN_SET);
  zassert_equal(mfd_npm1300_reg_write_fake.arg3_val, 1);
  zassert_true(opt3007_ready);
  zassert_equal(opt3007_emul.config & 0x0600, 0x0600, "not continuous");
}

ZTEST(light, test_enable_power_for_panel_skips_the_sensor_when_the_pmic_write_fails) {
  mfd_npm1300_reg_write_fake.return_val = -EIO;

  enable_power_for_panel();

  zassert_equal(opt3007_emul.transfers, 0);
  zassert_false(opt3007_ready);
}

ZTEST(light, test_disable_power_for_panel_switches_ldsw1_off_and_deinits_the_sensor) {
  init_and_settle();

  disable_power_for_panel();

  zassert_equal(mfd_npm1300_reg_write_fake.call_count, 1);
  zassert_equal_ptr(mfd_npm1300_reg_write_fake.arg0_val, G1_NPM1300_DEV);
  zassert_equal(mfd_npm1300_reg_write_fake.arg1_val, G1_NPM1300_LDSW_BASE);
  zassert_equal(mfd_npm1300_reg_write_fake.arg2_val, G1_NPM1300_LDSW1_EN_CLR);
  zassert_equal(mfd_npm1300_reg_write_fake.arg3_val, 1);
  zassert_false(opt3007_ready);
  zassert_equal(opt3007_read(0), -1);
}
