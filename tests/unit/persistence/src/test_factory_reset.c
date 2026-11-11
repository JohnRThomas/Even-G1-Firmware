/* reset_all_usr_data(): clears user data, keeps the factory record. */
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/bluetooth/bluetooth.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_bt.h"
#include "fakes_display.h"
#include "fakes_misc.h"
#include "fakes_storage.h"
#include "persistence.h"
#include "storage/storage.h"

static void lens_in_use(void)
{
	struct GlassesState *gs = g1_state();

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

	gs->bp.id = 0x11223344;
	gs->bp.wear_sum_time = 7200;
	gs->bp.version = 7;
	gs->bp.valid = 1;
	update_burial_point_to_flash(gs);

	for (int i = 0; i < 4; i++) {
		fill_flash(FLASH_QUICKNOTES + i * QUICKNOTE_LEN, QUICKNOTE_LEN, 0x00);
		put_flash(FLASH_QUICKNOTES + i * QUICKNOTE_LEN, "aa 4e 6f 74 65"); /* "Note" */
	}
	put_flash(FLASH_SETTING, "aa 03 01"); /* Japanese, dual dashboard */

	g1_unit_reboot();
	load_settings_from_flash(g1_state());
	zassert_equal(get_current_language(), 0x03);
	zassert_equal(g1_state()->message_stay_time, 30);
}

static void reset_user_data(int reboot)
{
	reset_all_usr_data(g1_state(), reboot);
	run_flash_ops_thread();
}

ZTEST(persistence, test_user_data_reset_removes_every_bond)
{
	lens_in_use();

	reset_user_data(1);

	zassert_equal(bt_unpair_fake.call_count, 1);
	zassert_equal(bt_unpair_fake.arg0_val, BT_ID_DEFAULT);
	zassert_is_null(bt_unpair_fake.arg1_val, "one address instead of all bonds");
}

ZTEST(persistence, test_user_data_reset_deletes_the_four_quick_notes)
{
	lens_in_use();

	reset_all_usr_data(g1_state(), 1);
	zassert_equal(getQuickNoteQueueNum(), 4);
	run_flash_ops_thread();

	expect_flash_fill(FLASH_QUICKNOTES, 4 * QUICKNOTE_LEN, 0xff);
}

ZTEST(persistence, test_user_data_reset_sets_english_and_dashboard_startup_mode_0)
{
	lens_in_use();

	reset_user_data(1);

	expect_flash(FLASH_SETTING, "aa 02 00");
	zassert_equal(get_current_language(), 2);
	zassert_equal(get_dashboard_startup_mode(), 0);
}

ZTEST(persistence, test_user_data_reset_writes_the_default_user_settings)
{
	lens_in_use();

	reset_user_data(1);

	expect_flash(FLASH_USR_SETTINGS,
		     /* 00 */ "0a 15 14 01  90 01 00 00  01 00 01 03  ff ff ff ff"
		     /* 10 */ "ff ff ff  01  00  00  00  00  03  0f  00 00  00 00 00 00"
		     /* 20 */ "00 00  64 00  00 00  00 00 00 00 00 00");
	zassert_equal(g1_state()->bonded, 0);
}

ZTEST(persistence, test_user_data_reset_erases_the_usage_statistics)
{
	lens_in_use();

	reset_user_data(1);

	expect_flash_fill(FLASH_BURIAL_POINT, BURIAL_POINT_LEN, 0xff);
}

ZTEST(persistence, test_user_data_reset_keeps_the_factory_record)
{
	lens_in_use();
	expect_flash(FLASH_SYS_SETTINGS, "01 00 00 00  00 00 00 00  2a");

	reset_user_data(1);

	expect_flash(FLASH_SYS_SETTINGS, "01 00 00 00  00 00 00 00  2a");
	expect_flash(FLASH_SYS_SETTINGS + 0x15, "53 31 30 30 64 65 6d 6f 64 65 76 69 63 65");
	expect_flash(FLASH_SYS_SETTINGS + 0x60, "20 00 10 00");
}

ZTEST(persistence, test_user_data_reset_clears_page_indices_retained_ram_and_master_audio)
{
	lens_in_use();

	reset_all_usr_data(g1_state(), 1);

	zassert_equal(cleanNotesIndex_fake.call_count, 1);
	zassert_equal(cleanStocksIndex_fake.call_count, 1);
	zassert_equal(cleanNewsIndex_fake.call_count, 1);
	zassert_equal(cleanCalenadrIndex_fake.call_count, 1);
	zassert_equal(deinit_ncs_memory_fake.call_count, 1);
	zassert_equal(delAudioStreamRecord_fake.call_count, 1);
	zassert_equal(delAudioStreamRecord_fake.arg0_val, 0xff);
}

ZTEST(persistence, test_user_data_reset_on_the_slave_has_no_audio_to_delete)
{
	lens_in_use();
	g1_unit_set_role(G1_ROLE_SLAVE);

	reset_user_data(1);

	zassert_equal(delAudioStreamRecord_fake.call_count, 0);
	zassert_equal(bt_unpair_fake.call_count, 1);
}

ZTEST(persistence, test_user_data_reset_with_reboot_reboots)
{
	lens_in_use();

	reset_user_data(1);

	zassert_equal(system_reboot_delayed_fake.call_count, 1);
	zassert_equal(set_shipmode_fake.call_count, 0);
}

ZTEST(persistence, test_user_data_reset_without_reboot_enters_ship_mode)
{
	lens_in_use();

	reset_user_data(0);

	zassert_equal(system_reboot_delayed_fake.call_count, 0);
	zassert_equal(set_shipmode_fake.call_count, 1);
	zassert_equal(set_shipmode_fake.arg0_val, 1);
}

ZTEST(persistence, test_after_user_data_reset_and_reboot_the_records_load_as_new)
{
	lens_in_use();
	reset_user_data(1);

	g1_unit_reboot();
	z_impl_sys_rand32_get_fake.return_val = 0x2468ace0;
	load_usr_setting(g1_state());
	load_burial_point(g1_state());

	zassert_equal(g1_state()->panel.panel_brightness_level, 0x15);
	zassert_equal(g1_state()->message_stay_time, 15);
	zassert_equal(g1_state()->bonded, 0);
	zassert_equal(g1_state()->bp.id, 0x2468ace0);
	zassert_equal(g1_state()->bp.wear_sum_time, 0);
	zassert_equal(get_current_language(), 2);
	zassert_equal(get_dashboard_startup_mode(), 0);
}
