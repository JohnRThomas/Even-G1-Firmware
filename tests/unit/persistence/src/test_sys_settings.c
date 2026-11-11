/* A valid word other than 1 is a first boot: defaults, written back. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_storage.h"
#include "persistence.h"
#include "storage/storage.h"

ZTEST_SUITE(persistence, NULL, NULL, NULL, NULL, NULL);

/* every field differs from its default and from 0 */
static void factory_values(struct GlassesState *gs)
{
	gs->aging_discharge_time_s = 0x11223344;
	gs->esb_channel = 0x2a;
	gs->imu.ref = -1234;
	gs->opt3007_context.lux_base_max = 80000;
	gs->panel.voltage = 1;
	memset(SERIAL_NUMBER, 0, sizeof(SERIAL_NUMBER));
	strcpy(SERIAL_NUMBER, "S110LAAL290026");
	memset(glasses_name, 0, sizeof(glasses_name));
	strcpy(glasses_name, "G1-HW-B");
	gs->esb_master_addr = 0xa1;
	gs->esb_slave_addr = 0xa2;
	gs->aging_test_count = 3;
	gs->aging_start_soc = 0x55;
	gs->aging_charge_time_s = 7200;
	gs->canvas_x = 0x20;
	gs->canvas_y = 0x10;
	gs->lux_base_low = 0x101;
	gs->lux_base_mid = 0x202;
	gs->lux_base_high = 0x303;
	gs->lux_base_spare = 0x404;
	gs->panel.lum_base_0 = 0x3f;
	gs->panel.lum_base_1 = 0x3e;
	gs->panel.lum_gear = 0x3d;
	gs->luminance_curve = 128000;
	gs->luminance_curve_a1 = 0x505;
	for (int i = 0; i < 6; i++) {
		gs->aging_discharge_step_s[i] = 600 * (i + 1);
	}
	gs->aging_elapsed_s = 86400;
	gs->aging_touch_count = 9;
	gs->aging_charge_check_result = 1;
}

static const char factory_record[] =
	/* 00 */ "01 00 00 00  44 33 22 11  2a 00 00 00  2e fb ff ff"
	/* 10 */ "80 38 01 00  01  53 31 31 30 4c 41 41 4c 32 39 30"   /* S110LAAL290 */
	/* 20 */ "30 32 36 00 00 00 00 00 00 00 00 00 00 00 00 00"     /* 026 */
	/* 30 */ "00 00 00 00 00  47 31 2d 48 57 2d 42 00 00 00 00"    /* G1-HW-B */
	/* 40 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 50 */ "00 00 00 00 00  a1  a2  03  55 00 00 00  20 1c 00 00"
	/* 60 */ "20 00  10 00  02 02 00 00  01 01 00 00  03 03 00 00"
	/* 70 */ "04 04 00 00  3f 00  3e 00  3d 00  00 00  00 f4 01 00"
	/* 80 */ "05 05 00 00  58 02 b0 04 08 07 60 09 b8 0b 10 0e"
	/* 90 */ "80 51 01 00  09  01  00 00";

ZTEST(persistence, test_save_sys_settings_writes_the_record_at_0x130000_byte_by_byte)
{
	factory_values(g1_state());

	zassert_equal(save_sys_settings(g1_state(), 0), 0);

	expect_flash(FLASH_SYS_SETTINGS, factory_record);
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 1);
	zassert_equal(flash_settings_write_and_verify_fake.arg0_val, FLASH_SYS_SETTINGS);
	zassert_equal(flash_settings_write_and_verify_fake.arg2_val, SYS_SETTINGS_LEN);
}

ZTEST(persistence, test_sys_settings_survive_a_reboot)
{
	struct GlassesState *gs;

	factory_values(g1_state());
	save_sys_settings(g1_state(), 0);

	g1_unit_reboot();
	gs = g1_state();
	zassert_equal(load_sys_setting(gs), 0);

	zassert_equal(gs->first_enter_flag, 1);
	zassert_equal(gs->aging_discharge_time_s, 0x11223344);
	zassert_equal(gs->esb_channel, 0x2a);
	zassert_equal(gs->imu.ref, -1234);
	zassert_equal(gs->opt3007_context.lux_base_max, 80000);
	zassert_equal(gs->panel.voltage, 1);
	expect_str(SERIAL_NUMBER, "S110LAAL290026");
	expect_str(glasses_name, "G1-HW-B");
	zassert_equal(gs->esb_master_addr, 0xa1);
	zassert_equal(gs->esb_slave_addr, 0xa2);
	zassert_equal(gs->aging_test_count, 3);
	zassert_equal(gs->aging_start_soc, 0x55);
	zassert_equal(gs->aging_charge_time_s, 7200);
	zassert_equal(gs->canvas_x, 0x20);
	zassert_equal(gs->canvas_y, 0x10);
	zassert_equal(gs->lux_base_low, 0x101);
	zassert_equal(gs->lux_base_mid, 0x202);
	zassert_equal(gs->lux_base_high, 0x303);
	zassert_equal(gs->lux_base_spare, 0x404);
	zassert_equal(gs->panel.lum_base_0, 0x3f);
	zassert_equal(gs->panel.lum_base_1, 0x3e);
	zassert_equal(gs->panel.lum_gear, 0x3d);
	zassert_equal(gs->luminance_curve, 128000);
	zassert_equal(gs->luminance_curve_a1, 0x505);
	for (int i = 0; i < 6; i++) {
		zassert_equal(gs->aging_discharge_step_s[i], 600 * (i + 1), "step %d", i);
	}
	zassert_equal(gs->aging_elapsed_s, 86400);
	zassert_equal(gs->aging_touch_count, 9);
	zassert_equal(gs->aging_charge_check_result, 1);
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);
}

ZTEST(persistence, test_first_boot_loads_the_defaults_and_writes_a_valid_record)
{
	struct GlassesState *gs = g1_state();

	zassert_equal(load_sys_setting(gs), 0);

	zassert_equal(gs->first_enter_flag, 1);
	zassert_equal(gs->esb_channel, 0xff);
	zassert_equal(gs->aging_discharge_time_s, 0);
	zassert_equal(gs->opt3007_context.lux_base_max, 80000);
	zassert_equal(gs->imu.ref, 0);
	zassert_equal(gs->panel.voltage, 0);
	zassert_equal(gs->canvas_x, 0x20);
	zassert_equal(gs->canvas_y, 0x20);
	zassert_equal(gs->luminance_curve, 128000);
	zassert_equal(gs->panel.lum_base_0, 0x3f);
	zassert_equal(gs->panel.lum_base_1, 0x3f);
	zassert_equal(gs->panel.lum_gear, 0x3f);
	zassert_equal(gs->aging_touch_count, 0);
	expect_str(SERIAL_NUMBER, "S100demodevice");
	expect_str(glasses_name, "S100demoglasses");

	expect_flash(FLASH_SYS_SETTINGS, "01 00 00 00  00 00 00 00  ff");
	expect_flash(FLASH_SYS_SETTINGS + 0x10, "80 38 01 00  00"
						"53 31 30 30 64 65 6d 6f 64 65 76 69 63 65 00 00");
	expect_flash(FLASH_SYS_SETTINGS + 0x35,
		     "53 31 30 30 64 65 6d 6f 67 6c 61 73 73 65 73 00");
	expect_flash(FLASH_SYS_SETTINGS + 0x60, "20 00 20 00");
	expect_flash(FLASH_SYS_SETTINGS + 0x74, "3f 00 3f 00 3f 00 00 00  00 f4 01 00");
}

ZTEST(persistence, test_record_with_valid_0_is_a_first_boot)
{
	struct GlassesState *gs = g1_state();

	factory_values(gs);
	save_sys_settings(gs, 0);
	put_flash(FLASH_SYS_SETTINGS, "00 00 00 00");

	g1_unit_reboot();
	gs = g1_state();
	load_sys_setting(gs);

	zassert_equal(gs->esb_channel, 0xff);
	zassert_equal(gs->aging_discharge_time_s, 0);
	zassert_equal(gs->canvas_y, 0x20);
	zassert_equal(gs->panel.lum_gear, 0x3f);
	zassert_equal(gs->luminance_curve_a1, 0);
	expect_flash(FLASH_SYS_SETTINGS, "01 00 00 00  00 00 00 00  ff");
}

ZTEST(persistence, test_corrupt_record_gets_safe_values_for_the_checked_fields)
{
	struct GlassesState *gs = g1_state();

	put_flash(FLASH_SYS_SETTINGS, "01 00 00 00");

	zassert_equal(load_sys_setting(gs), 0);

	zassert_equal(gs->panel.voltage, 0);
	zassert_equal(gs->luminance_curve, 128000);
	zassert_equal(gs->canvas_x, 0x20);
	zassert_equal(gs->canvas_y, 0x20);
	zassert_equal(gs->panel.lum_gear, 0x3f);
	zassert_equal(gs->aging_touch_count, 0);
	expect_str(SERIAL_NUMBER, "S100demodevice");
	expect_str(glasses_name, "S100demoglasses");
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);
}

ZTEST(persistence, test_out_of_range_canvas_and_curve_values_are_replaced_on_load)
{
	struct GlassesState *gs = g1_state();

	factory_values(gs);
	gs->canvas_x = 21;
	gs->canvas_y = 3;        /* clamped to 4 */
	gs->panel.lum_gear = 64;
	gs->panel.voltage = 2;   /* only 1 is kept */
	gs->luminance_curve = 0;
	gs->aging_touch_count = 0xff;
	save_sys_settings(gs, 0);

	g1_unit_reboot();
	gs = g1_state();
	load_sys_setting(gs);

	zassert_equal(gs->canvas_x, 0x20);
	zassert_equal(gs->canvas_y, 4);
	zassert_equal(gs->panel.lum_gear, 0x3f);
	zassert_equal(gs->panel.voltage, 0);
	zassert_equal(gs->luminance_curve, 128000);
	zassert_equal(gs->aging_touch_count, 0);
}

ZTEST(persistence, test_canvas_limits_22_42_and_4_64_are_kept)
{
	struct GlassesState *gs = g1_state();

	factory_values(gs);
	gs->canvas_x = 42;
	gs->canvas_y = 64;
	save_sys_settings(gs, 0);
	g1_unit_reboot();
	load_sys_setting(g1_state());
	zassert_equal(g1_state()->canvas_x, 42);
	zassert_equal(g1_state()->canvas_y, 64);

	factory_values(g1_state());
	g1_state()->canvas_x = 22;
	g1_state()->canvas_y = 4;
	save_sys_settings(g1_state(), 0);
	g1_unit_reboot();
	load_sys_setting(g1_state());
	zassert_equal(g1_state()->canvas_x, 22);
	zassert_equal(g1_state()->canvas_y, 4);
}

ZTEST(persistence, test_sys_settings_read_error_returns_minus_1_and_changes_nothing)
{
	struct GlassesState *gs = g1_state();

	gs->esb_channel = 0x2a;
	gs->canvas_y = 0x10;
	flash_settings_read_fake.custom_fake = NULL;
	flash_settings_read_fake.return_val = -EIO;

	zassert_equal(load_sys_setting(gs), -1);

	zassert_equal(gs->esb_channel, 0x2a);
	zassert_equal(gs->canvas_y, 0x10);
	expect_str(SERIAL_NUMBER, "S100demodevice");
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);
	expect_flash_fill(FLASH_SYS_SETTINGS, SYS_SETTINGS_LEN, 0xff);
}

ZTEST(persistence, test_serial_number_is_stored_in_16_bytes)
{
	struct GlassesState *gs = g1_state();

	factory_values(gs);
	strcpy(SERIAL_NUMBER, "ABCDEFGHIJKLMNOPQRSTU");
	save_sys_settings(gs, 0);

	expect_flash(FLASH_SYS_SETTINGS + 0x15,
		     "41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e 4f 50"  /* A..P */
		     "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
}
