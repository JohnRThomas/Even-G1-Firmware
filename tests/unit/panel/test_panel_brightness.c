/*
 * Brightness level to LUM (36h) / CUR (46h). At lum gear 63, level 0 is LUM 14 * 111 = 0x0612, CUR
 * 0; levels from 6 have LUM 64 * 111 = 0x1BC0, CUR level - 6. jbd_panel.c is #included for its
 * static register cache.
 */
#include "panel_support.h"

/* jbd_panel.c calls these before it defines them, and no firmware header declares them */
int panel_off(struct jbd_panel_context *panel);
int panel_on(struct jbd_panel_context *panel);

#include "panel_display/jbd_panel.c"

#define LUM_MAX_90HZ 7109
#define CUR_MAX      63
#define LEVEL_MAX    0x2a

static void panel_brightness_before(void *fixture)
{
  ARG_UNUSED(fixture);
  JBD_LUM_REG_VAL = 0;
  JBD_CUR_REG_VAL = 0;
}

ZTEST_SUITE(panel_brightness, NULL, NULL, panel_brightness_before, NULL, NULL);

static void curve(uint8_t level, uint16_t *lum, uint8_t *cur)
{
  *lum = 0xdead;
  *cur = 0xee;
  update_brightness_level_for_jdb_panel(level, lum, cur);
}

static void set_gear(uint16_t gear)
{
  panel_ctx()->lum_gear = gear;
  panel_ctx()->brightness_max = gear;
}

ZTEST(panel_brightness, test_curve_end_points_at_the_factory_gear)
{
  uint16_t lum;
  uint8_t cur;

  curve(0, &lum, &cur);
  zassert_equal(lum, 0x0612, "level 0: LUM 0x%04x", lum);
  zassert_equal(cur, 0);

  curve(LEVEL_MAX, &lum, &cur);
  zassert_equal(lum, 0x1bc0, "level 0x2a: LUM 0x%04x", lum);
  zassert_equal(cur, 36);
}

ZTEST(panel_brightness, test_curve_stays_in_the_register_ranges_for_every_gear)
{
  for (uint16_t gear = 0; gear <= 63; gear++) {
    set_gear(gear);
    for (int level = 0; level <= LEVEL_MAX; level++) {
      uint16_t lum;
      uint8_t cur;

      curve(level, &lum, &cur);
      zassert_true(lum >= 1 && lum <= LUM_MAX_90HZ, "gear %u level %d: LUM %u",
             gear, level, lum);
      zassert_true(cur <= CUR_MAX, "gear %u level %d: CUR %u", gear, level, cur);
    }
  }
}

/* 1 brighter, 0 same, -1 darker */
static int level_step(int level)
{
  uint16_t lum0, lum1;
  uint8_t cur0, cur1;

  curve(level - 1, &lum0, &cur0);
  curve(level, &lum1, &cur1);
  if (lum1 < lum0 || cur1 < cur0) {
    return -1;
  }
  return (lum1 > lum0 || cur1 > cur0) ? 1 : 0;
}

ZTEST(panel_brightness, test_every_level_step_is_brighter_at_the_factory_gear)
{
  for (int level = 1; level <= LEVEL_MAX; level++) {
    zassert_equal(level_step(level), 1, "level %d is not brighter than level %d",
            level, level - 1);
  }
}

/* integer steps repeat values at low gears */
ZTEST(panel_brightness, test_no_level_step_is_darker_for_any_gear)
{
  for (uint16_t gear = 0; gear <= 63; gear++) {
    set_gear(gear);
    for (int level = 1; level <= LEVEL_MAX; level++) {
      zassert_true(level_step(level) >= 0, "gear %u: level %d darker than %d",
             gear, level, level - 1);
    }
  }
}

ZTEST(panel_brightness, test_levels_below_6_ramp_lum_at_the_lowest_current)
{
  uint16_t full, lum;
  uint8_t cur;

  curve(6, &full, &cur);
  zassert_equal(cur, 0);
  for (int level = 0; level <= 5; level++) {
    curve(level, &lum, &cur);
    zassert_equal(cur, 0, "level %d", level);
    zassert_true(lum < full, "level %d: LUM %u, full scale %u", level, lum, full);
  }
}

/* includes the factory level 0x15, which the panel init sequence writes as LUM 0x1BC0, CUR 0x0F */
ZTEST(panel_brightness, test_levels_from_6_raise_the_current_one_step_each)
{
  uint16_t lum;
  uint8_t cur;

  for (int level = 6; level <= LEVEL_MAX; level++) {
    curve(level, &lum, &cur);
    zassert_equal(lum, 0x1bc0, "level %d", level);
    zassert_equal(cur, level - 6, "level %d", level);
  }
}

ZTEST(panel_brightness, test_percent_scales_lum_only)
{
  uint16_t lum;
  uint8_t cur;

  panel_ctx()->brightness_percent = 80;
  curve(PANEL_DEFAULT_LEVEL, &lum, &cur);
  zassert_equal(lum, 5683); /* 7104 * 80 / 100 */
  zassert_equal(cur, 0x0f);

  panel_ctx()->brightness_percent = 70;
  curve(PANEL_DEFAULT_LEVEL, &lum, &cur);
  zassert_equal(lum, 4972); /* 7104 * 70 / 100 */

  panel_ctx()->brightness_percent = 100;
  curve(PANEL_DEFAULT_LEVEL, &lum, &cur);
  zassert_equal(lum, 0x1bc0);
}

ZTEST(panel_brightness, test_lowest_settings_keep_the_panel_lit)
{
  uint16_t lum;
  uint8_t cur;

  set_gear(0);
  panel_ctx()->brightness_percent = 70;
  curve(0, &lum, &cur);

  zassert_equal(lum, 1);
  zassert_equal(cur, 0);
}

/* CUR stays in 6 bits: from 0x46, level - 6 would reach the reserved bits 7:6 */
ZTEST(panel_brightness, test_out_of_range_level_keeps_reserved_current_bits_clear)
{
  static const uint8_t levels[] = {0x46, 0x80, 0xff};

  for (size_t index = 0; index < ARRAY_SIZE(levels); index++) {
    uint16_t lum;
    uint8_t cur;

    curve(levels[index], &lum, &cur);
    zassert_true(cur <= CUR_MAX, "level 0x%02x: CUR 0x%02x", levels[index], cur);
  }
}

ZTEST(panel_brightness, test_set_brightness_sends_the_recorded_sequence)
{
  panel_running();

  set_brightness_to_panel_reg(PANEL_DEFAULT_LEVEL);

  expect_xfers("06", "a9", "36 1b c0", "46 0f", "31 04", "a3", "97");
}

ZTEST(panel_brightness, test_set_brightness_puts_the_curve_on_the_wire_for_every_level)
{
  panel_running();

  for (int level = 0; level <= LEVEL_MAX; level++) {
    char lum_hex[16], cur_hex[16];
    uint16_t lum;
    uint8_t cur;

    curve(level, &lum, &cur);
    snprintf(lum_hex, sizeof(lum_hex), "36 %02x %02x", lum >> 8, lum & 0xff);
    snprintf(cur_hex, sizeof(cur_hex), "46 %02x", cur);
    g1_fake_spim_clear_log();

    set_brightness_to_panel_reg(level);

    zassert_equal(g1_fake_spim.xfers, 7, "level %d", level);
    expect_xfer(2, lum_hex);
    expect_xfer(3, cur_hex);
  }
}

ZTEST(panel_brightness, test_running_change_waits_for_panel_init)
{
  panel_running();
  panel_ctx()->init_done = 0;

  set_brightness_to_panel_reg_in_running(PANEL_DEFAULT_LEVEL);

  zassert_equal(g1_fake_spim.xfers, 0);
}

ZTEST(panel_brightness, test_running_change_writes_both_registers_the_first_time)
{
  panel_running();

  set_brightness_to_panel_reg_in_running(PANEL_DEFAULT_LEVEL);

  expect_xfers("06", "a9", "36 1b c0", "46 0f", "31 04", "a3", "97");
}

ZTEST(panel_brightness, test_running_change_skips_an_unchanged_register)
{
  panel_running();
  set_brightness_to_panel_reg_in_running(PANEL_DEFAULT_LEVEL);
  g1_fake_spim_clear_log();

  set_brightness_to_panel_reg_in_running(PANEL_DEFAULT_LEVEL + 1);

  expect_xfers("06", "a9", "46 10", "31 04", "a3", "97");
}

/* the last LUM and CUR values written */
static void panel_registers(uint16_t *lum, uint8_t *cur)
{
  for (int index = 0; index < g1_fake_spim.xfers; index++) {
    const struct g1_spim_entry *entry = panel_xfer(index);

    if (entry->len == 3 && entry->mosi[0] == SPI_WR_LUM_REG) {
      *lum = sys_get_be16(&entry->mosi[1]);
    } else if (entry->len == 2 && entry->mosi[0] == SPI_WR_CURRENT_REG) {
      *cur = entry->mosi[1];
    }
  }
}

ZTEST(panel_brightness, test_running_changes_keep_the_registers_on_the_curve)
{
  /* panel reset values */
  uint16_t lum = 0x1bc5;
  uint8_t cur = 0x00;

  panel_running();
  for (int step = 0; step <= 2 * LEVEL_MAX; step++) {
    int level = step <= LEVEL_MAX ? step : 2 * LEVEL_MAX - step;
    uint16_t want_lum;
    uint8_t want_cur;

    curve(level, &want_lum, &want_cur);
    g1_fake_spim_clear_log();

    set_brightness_to_panel_reg_in_running(level);

    panel_registers(&lum, &cur);
    zassert_equal(lum, want_lum, "level %d: panel LUM %u", level, lum);
    zassert_equal(cur, want_cur, "level %d: panel CUR %u", level, cur);
  }
}

ZTEST(panel_brightness, test_wr_lum_and_cur_reg_writes_only_what_changed)
{
  panel_running();

  wr_lum_and_cur_reg(0x0100, 5);
  wr_lum_and_cur_reg(0x0100, 6);
  wr_lum_and_cur_reg(0x0200, 6);

  expect_xfers("36 01 00", "46 05", "46 06", "36 02 00");
  zassert_equal(JBD_LUM_REG_VAL, 0x0200);
  zassert_equal(JBD_CUR_REG_VAL, 6);
}

/* direct writes update the register cache */
ZTEST(panel_brightness, test_running_change_after_a_direct_write_reaches_the_panel)
{
  panel_running();
  set_brightness_to_panel_reg_in_running(6);
  set_brightness_to_panel_reg(10);
  g1_fake_spim_clear_log();

  set_brightness_to_panel_reg_in_running(6);

  zassert_true(g1_fake_spim_find_cmd(SPI_WR_CURRENT_REG) >= 0,
         "the current register still holds level 10's CUR 4");
  expect_xfer(2, "46 00");
}

ZTEST(panel_brightness, test_set_brightness_callback_stores_the_level_only)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_running();

  zassert_equal(panel->set_brightness_cb(panel, 0x20), 0x20);

  zassert_equal(panel->panel_brightness_level, 0x20);
  zassert_equal(g1_fake_spim.xfers, 0);
}
