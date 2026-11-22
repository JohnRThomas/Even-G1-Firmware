/* An erased user record (display mode and 3-DoF enable FF) loads as the defaults. */
#include <zephyr/bluetooth/bluetooth.h>

#include "persistence.h"

static void user_values(struct GlassesState *gs) {
  static const bt_addr_le_t phone = {.type = 1, .a = {{0x11, 0x22, 0x33, 0x44, 0x55, 0xc6}}};

  gs->display_mode = 10;
  gs->panel.panel_brightness_level = 0x13;
  gs->imu.level = 30;
  gs->imu.dof3_enable = 1;
  gs->imu.div = 400;
  gs->opt3007_context.auto_brightness = 1;
  gs->bonded = 1;
  gs->check_mode = 1;
  gs->panel.raster_height_offset = 5;
  gs->bonded_addr = phone;
  gs->imu.wear = 1;
  gs->message_mode = 1;
  gs->charge_limit_upper = 80;
  gs->charge_limit_lower = 20;
  gs->panel.canvas_distance_gear = 4;
  gs->message_stay_time = 30;
  gs->double_tap_customize = 2;
  gs->imu.base = 1234;
  gs->dashboard_ts->date_format = 1;
  gs->panel.brightness_percent = 90;
  gs->longpress_tap_customize = 1;
  gs->head_up_mic_disabled = 0;
}

ZTEST(persistence, test_save_usr_settings_writes_the_record_at_0x134000_byte_by_byte) {
  user_values(g1_state());

  zassert_equal(save_usr_settings(g1_state(), 0), 0);

  expect_flash(FLASH_USR_SETTINGS,
               /* 00 */ "0a 13 1e 01  90 01 00 00  01 01 01 05  01 11 22 33"
                        /* 10 */ "44 55 c6  01  01  50  14  00  04  1e  02 00  d2 04 00 00"
                        /* 20 */ "01 00  5a 00  01 00  00 00 00 00 00 00");
}

ZTEST(persistence, test_usr_settings_survive_a_reboot) {
  struct GlassesState *gs = g1_state();

  user_values(gs);
  save_usr_settings(gs, 0);

  g1_unit_reboot();
  zassert_equal(load_usr_setting(gs), 0);

  zassert_equal(gs->display_mode, 10);
  zassert_equal(gs->panel.panel_brightness_level, 0x13);
  zassert_equal(gs->imu.level, 30);
  zassert_equal(gs->imu.dof3_enable, 1);
  zassert_equal(gs->imu.div, 400);
  zassert_equal(gs->opt3007_context.auto_brightness, 1);
  zassert_equal(gs->bonded, 1);
  zassert_equal(gs->check_mode, 1);
  zassert_equal(gs->panel.raster_height_offset, 5);
  expect_bytes(&gs->bonded_addr, sizeof(gs->bonded_addr), "01 11 22 33 44 55 c6");
  zassert_equal(gs->imu.wear, 1);
  zassert_equal(gs->message_mode, 1);
  zassert_equal(gs->charge_limit_upper, 80);
  zassert_equal(gs->charge_limit_lower, 20);
  zassert_equal(gs->panel.canvas_distance_gear, 4);
  zassert_equal(gs->message_stay_time, 30);
  zassert_equal(gs->double_tap_customize, 2);
  zassert_equal(gs->imu.base, 1234);
  zassert_equal(gs->dashboard_ts->date_format, 1);
  zassert_equal(gs->panel.brightness_percent, 90);
  zassert_equal(gs->longpress_tap_customize, 1);
  zassert_equal(gs->head_up_mic_disabled, 0);
  zassert_equal(set_test_mode_fake.call_count, 1);
  zassert_equal(set_test_mode_fake.arg0_val, 0);
  zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);
}

static const char default_user_record[] =
  /* 00 */ "0a 15 14 01  90 01 00 00  01 00 01 03  ff ff ff ff"
           /* 10 */ "ff ff ff  01  00  00  00  00  03  0f  00 00  00 00 00 00"
           /* 20 */ "00 00  64 00  00 00  00 00 00 00 00 00";

/* the GlassesState fields load_usr_setting() range-checks, at their defaults */
static void expect_range_checked_defaults(struct GlassesState *gs) {
  zassert_equal(gs->imu.div, 400);
  zassert_equal(gs->panel.raster_height_offset, 3);
  zassert_equal(gs->message_mode, 0);
  zassert_equal(gs->panel.canvas_distance_gear, 3);
  zassert_equal(gs->message_stay_time, 15);
  zassert_equal(gs->double_tap_customize, 0);
  zassert_equal(gs->imu.base, 0);
  zassert_equal(gs->dashboard_ts->date_format, 0);
  zassert_equal(gs->panel.brightness_percent, 100);
  zassert_equal(gs->longpress_tap_customize, 0);
  zassert_equal(gs->head_up_mic_disabled, 0);
}

ZTEST(persistence, test_erased_user_record_is_replaced_by_the_defaults) {
  struct GlassesState *gs = g1_state();

  zassert_equal(load_usr_setting(gs), 0);

  zassert_equal(gs->display_mode, 10);
  zassert_equal(gs->panel.panel_brightness_level, 0x15);
  zassert_equal(gs->imu.level, 0x14);
  zassert_equal(gs->imu.dof3_enable, 1);
  zassert_equal(gs->opt3007_context.auto_brightness, 1);
  zassert_equal(gs->bonded, 0);
  zassert_equal(gs->check_mode, 1);
  zassert_equal(gs->imu.wear, 1);
  expect_range_checked_defaults(gs);
  expect_flash(FLASH_USR_SETTINGS, default_user_record);
}

ZTEST(persistence, test_clear_user_settings_writes_the_default_record) {
  user_values(g1_state());
  save_usr_settings(g1_state(), 0);

  zassert_equal(clear_user_settings(g1_state()), 0);

  expect_flash(FLASH_USR_SETTINGS, default_user_record);
  zassert_equal(set_test_mode_fake.call_count, 1);
  zassert_equal(set_test_mode_fake.arg0_val, 0);
}

ZTEST(persistence, test_out_of_range_user_values_get_their_defaults_on_load) {
  struct GlassesState *gs = g1_state();

  put_flash(FLASH_USR_SETTINGS,
            /* 00 */ "0a 13 1e 01  00 00 00 00  01 00 01 09  ff ff ff ff"
                     /* 10 */ "ff ff ff  01  02  00  00  02  0a  00  06 00  ff ff ff ff"
                     /* 20 */ "03 00  65 00  02 02  00 00 00 00 00 00");

  zassert_equal(load_usr_setting(gs), 0);

  expect_range_checked_defaults(gs);
  zassert_equal(set_test_mode_fake.arg0_val, 0);
  zassert_equal(gs->panel.panel_brightness_level, 0x13);
  zassert_equal(gs->imu.level, 30);
}

ZTEST(persistence, test_user_value_limits_are_kept) {
  struct GlassesState *gs = g1_state();

  put_flash(FLASH_USR_SETTINGS,
            /* 00 */ "0a 13 1e 01  90 01 00 00  01 00 01 08  ff ff ff ff"
                     /* 10 */ "ff ff ff  01  01  00  00  00  09  ff  05 00  00 00 00 00"
                     /* 20 */ "02 00  46 00  01 01  00 00 00 00 00 00");
  /* stay time FF reads as erased, not as a limit */

  load_usr_setting(gs);

  zassert_equal(gs->panel.raster_height_offset, 8);
  zassert_equal(gs->message_mode, 1);
  zassert_equal(gs->panel.canvas_distance_gear, 9);
  zassert_equal(gs->message_stay_time, 15);
  zassert_equal(gs->double_tap_customize, 5);
  zassert_equal(gs->dashboard_ts->date_format, 2);
  zassert_equal(gs->panel.brightness_percent, 70);
  zassert_equal(gs->longpress_tap_customize, 1);
  zassert_equal(gs->head_up_mic_disabled, 1);
}

ZTEST(persistence, test_stored_display_mode_11_is_rewritten_as_10) {
  struct GlassesState *gs = g1_state();

  user_values(gs);
  gs->display_mode = 11;
  save_usr_settings(gs, 0);
  expect_flash(FLASH_USR_SETTINGS, "0b");

  g1_unit_reboot();
  load_usr_setting(gs);

  zassert_equal(gs->display_mode, 10);
  expect_flash(FLASH_USR_SETTINGS, "0a 13 1e 01");
}

static uint8_t pt_test_mode;

static void fake_set_test_mode(uint8_t mode) {
  pt_test_mode = mode;
}

static uint8_t fake_get_pt_test_mode(void) {
  return pt_test_mode;
}

ZTEST(persistence, test_production_mode_record_restores_its_panel_settings) {
  struct GlassesState *gs = g1_state();

  pt_test_mode = 1;
  set_test_mode_fake.custom_fake = fake_set_test_mode;
  get_pt_test_mode_fake.custom_fake = fake_get_pt_test_mode;
  user_values(gs);
  save_usr_settings(gs, 0);
  expect_flash(FLASH_USR_SETTINGS + 0x17, "01");

  g1_unit_reboot();
  pt_test_mode = 0;
  set_test_mode_fake.custom_fake = fake_set_test_mode;
  get_pt_test_mode_fake.custom_fake = fake_get_pt_test_mode;
  load_usr_setting(gs);

  zassert_equal(pt_test_mode, 1);
  zassert_equal(gs->display_mode, 11);
  zassert_equal(gs->opt3007_context.auto_brightness, 0);
  zassert_equal(gs->panel.panel_brightness_level, 0x15);
  zassert_equal(gs->panel.raster_height_offset, 3);
  zassert_equal(gs->panel.canvas_distance_gear, 3);
}

ZTEST(persistence, test_usr_settings_read_error_returns_minus_1_and_changes_nothing) {
  struct GlassesState *gs = g1_state();

  gs->message_stay_time = 30;
  G1_FAKE_RETURN(flash_settings_read, -EIO);

  zassert_equal(load_usr_setting(gs), -1);

  zassert_equal(gs->message_stay_time, 30);
  zassert_equal(set_test_mode_fake.call_count, 0);
  zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);
}

/* reset_all_usr_data(): clears user data, keeps the factory record. */

/* before each user_data_reset test: every record and store written, then a reboot */
static void lens_in_use(void *fixture) {
  struct GlassesState *gs = g1_state();

  ARG_UNUSED(fixture);
  gs->esb_channel = 0x2a;
  gs->canvas_x = 0x20;
  gs->canvas_y = 0x10;
  save_sys_settings(gs, 0);

  gs->display_mode = 10;
  gs->panel.panel_brightness_level = 0x13;
  gs->imu.dof3_enable = 1;
  gs->bonded = 1;
  gs->message_stay_time = 30;
  save_usr_settings(gs, 0);

  /* usage statistics left by an earlier firmware */
  put_flash(FLASH_USAGE_STATS, "44 33 22 11  20 1c 00 00  00 00 00 00  00 00 00 00"
                               "  00 00 00 00  07 00 01");

  memset(&g1_fake_flash_image[FLASH_QUICKNOTES], 0x00, 4 * QUICKNOTE_LEN);
  for (int slot = 0; slot < 4; slot++) {
    put_flash(FLASH_QUICKNOTES + slot * QUICKNOTE_LEN, "aa 4e 6f 74 65"); /* "Note" */
  }
  put_flash(FLASH_SETTING, "aa 03 01"); /* Japanese, dual dashboard */

  g1_unit_reboot();
  load_settings_from_flash(gs);
  zassert_equal(get_current_language(), 0x03);
  zassert_equal(gs->message_stay_time, 30);
}

ZTEST_SUITE(user_data_reset, NULL, NULL, lens_in_use, NULL, NULL);

static void reset_user_data(int reboot) {
  reset_all_usr_data(g1_state(), reboot);
  run_flash_ops_thread();
}

ZTEST(user_data_reset, test_removes_every_bond) {
  reset_user_data(1);

  zassert_equal(bt_unpair_fake.call_count, 1);
  zassert_equal(bt_unpair_fake.arg0_val, BT_ID_DEFAULT);
  zassert_is_null(bt_unpair_fake.arg1_val, "one address instead of all bonds");
}

ZTEST(user_data_reset, test_deletes_the_four_quick_notes) {
  reset_all_usr_data(g1_state(), 1);
  zassert_equal(getQuickNoteQueueNum(), 4);
  run_flash_ops_thread();

  expect_flash_fill(FLASH_QUICKNOTES, 4 * QUICKNOTE_LEN, 0xff);
}

ZTEST(user_data_reset, test_sets_english_and_dashboard_startup_mode_0) {
  reset_user_data(1);

  expect_flash(FLASH_SETTING, "aa 02 00");
  zassert_equal(get_current_language(), 2);
  zassert_equal(get_dashboard_startup_mode(), 0);
}

ZTEST(user_data_reset, test_writes_the_default_user_settings) {
  reset_user_data(1);

  expect_flash(FLASH_USR_SETTINGS, default_user_record);
  zassert_equal(g1_state()->bonded, 0);
}

ZTEST(user_data_reset, test_erases_the_usage_statistics_of_earlier_firmware) {
  expect_flash(FLASH_USAGE_STATS, "44 33 22 11");

  reset_user_data(1);

  expect_flash_fill(FLASH_USAGE_STATS, USAGE_STATS_LEN, 0xff);
}

ZTEST(user_data_reset, test_keeps_the_factory_record) {
  expect_flash(FLASH_SYS_SETTINGS, "01 00 00 00  00 00 00 00  2a");

  reset_user_data(1);

  expect_flash(FLASH_SYS_SETTINGS, "01 00 00 00  00 00 00 00  2a");
  expect_flash(FLASH_SYS_SETTINGS + 0x15, "53 31 30 30 64 65 6d 6f 64 65 76 69 63 65");
  expect_flash(FLASH_SYS_SETTINGS + 0x60, "20 00 10 00");
}

ZTEST(user_data_reset, test_clears_page_indices_retained_ram_and_master_audio) {
  reset_all_usr_data(g1_state(), 1);

  zassert_equal(cleanNotesIndex_fake.call_count, 1);
  zassert_equal(cleanStocksIndex_fake.call_count, 1);
  zassert_equal(cleanNewsIndex_fake.call_count, 1);
  zassert_equal(cleanCalenadrIndex_fake.call_count, 1);
  zassert_equal(deinit_ncs_memory_fake.call_count, 1);
  zassert_equal(delAudioStreamRecord_fake.call_count, 1);
  zassert_equal(delAudioStreamRecord_fake.arg0_val, 0xff);
}

ZTEST(user_data_reset, test_on_the_slave_has_no_audio_to_delete) {
  g1_unit_set_role(G1_ROLE_SLAVE);

  reset_user_data(1);

  zassert_equal(delAudioStreamRecord_fake.call_count, 0);
  zassert_equal(bt_unpair_fake.call_count, 1);
}

ZTEST(user_data_reset, test_with_reboot_reboots) {
  reset_user_data(1);

  zassert_equal(system_reboot_delayed_fake.call_count, 1);
  zassert_equal(set_shipmode_fake.call_count, 0);
}

ZTEST(user_data_reset, test_without_reboot_enters_ship_mode) {
  reset_user_data(0);

  zassert_equal(system_reboot_delayed_fake.call_count, 0);
  zassert_equal(set_shipmode_fake.call_count, 1);
  zassert_equal(set_shipmode_fake.arg0_val, 1);
}

ZTEST(user_data_reset, test_after_a_reboot_the_records_load_as_new) {
  reset_user_data(1);

  g1_unit_reboot();
  load_usr_setting(g1_state());

  zassert_equal(g1_state()->panel.panel_brightness_level, 0x15);
  zassert_equal(g1_state()->message_stay_time, 15);
  zassert_equal(g1_state()->bonded, 0);
  zassert_equal(get_current_language(), 2);
  zassert_equal(get_dashboard_startup_mode(), 0);
}
