/* Record: AA, language (02 English), startup mode (0 full, 1 dual, 2 minimal). */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_storage.h"
#include "persistence.h"
#include "storage/storage.h"

#define SETTING_QUEUE_DEPTH 5

ZTEST(persistence, test_language_is_stored_as_aa_language_and_read_back_after_a_reboot)
{
	zassert_equal(upgradeAppLanguageInfoToFlash(0x03), 0);
	run_flash_ops_thread();

	expect_flash(FLASH_SETTING, "aa 03");
	zassert_equal(get_current_language(), 0x03);

	g1_unit_reboot();
	zassert_equal(get_current_language(), 0x03);
	zassert_equal(get_dashboard_startup_mode(), 0);
}

ZTEST(persistence, test_lens_without_a_setting_record_is_english_with_startup_mode_0)
{
	zassert_equal(getAppLanguageInfofromFlash(), 0x02);
	zassert_equal(getDashboardStartUpModeInfofromFlash(), 0);
}

ZTEST(persistence, test_language_and_startup_mode_share_the_record)
{
	upgradeAppLanguageInfoToFlash(0x0e);
	upgradeDashboardStartupModeInfoToFlash(1);
	run_flash_ops_thread();

	expect_flash(FLASH_SETTING, "aa 0e 01");

	g1_unit_reboot();
	zassert_equal(get_current_language(), 0x0e);
	zassert_equal(get_dashboard_startup_mode(), 1);
}

ZTEST(persistence, test_each_queued_setting_gives_flash_ops_sem_once)
{
	upgradeAppLanguageInfoToFlash(0x04);
	zassert_equal(k_sem_count_get(&flash_ops_sem), 1);
	upgradeDashboardStartupModeInfoToFlash(2);
	cleanAppLanguageInfo();
	cleanDashBoardStartUpModeInfo();
	zassert_equal(k_sem_count_get(&flash_ops_sem), 4);
	zassert_equal(getSettingStorageQueueNum(), 4);

	expect_flash_fill(FLASH_SETTING, 3, 0xff);
}

ZTEST(persistence, test_queued_settings_are_applied_in_order)
{
	upgradeAppLanguageInfoToFlash(0x04);
	upgradeDashboardStartupModeInfoToFlash(2);
	upgradeAppLanguageInfoToFlash(0x06);
	upgradeDashboardStartupModeInfoToFlash(1);
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa 06 01");

	upgradeAppLanguageInfoToFlash(0x05);
	cleanAppLanguageInfo();
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa 02 01");

	cleanDashBoardStartUpModeInfo();
	upgradeDashboardStartupModeInfoToFlash(2);
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa 02 02");
	zassert_equal(getSettingStorageQueueNum(), 0);
}

ZTEST(persistence, test_setting_queue_holds_5_commands_and_rejects_the_6th)
{
	for (int i = 0; i < SETTING_QUEUE_DEPTH; i++) {
		zassert_equal(upgradeAppLanguageInfoToFlash(0x01 + i), 0, "command %d", i);
	}

	zassert_equal(upgradeAppLanguageInfoToFlash(0x07), -1);
	zassert_equal(cleanAppLanguageInfo(), -1);
	zassert_equal(upgradeDashboardStartupModeInfoToFlash(1), -1);
	zassert_equal(cleanDashBoardStartUpModeInfo(), -1);
	zassert_equal(k_sem_count_get(&flash_ops_sem), SETTING_QUEUE_DEPTH);

	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa 05");
}

ZTEST(persistence, test_language_0_is_rejected)
{
	put_flash(FLASH_SETTING, "aa 03 01");
	g1_unit_reboot();

	upgradeAppLanguageInfoToFlash(0x00);
	zassert_equal(SettingStoreHandler(), -1);

	expect_flash(FLASH_SETTING, "aa 03 01");
	zassert_equal(get_current_language(), 0x03);
	zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
}

ZTEST(persistence, test_same_language_is_not_rewritten)
{
	put_flash(FLASH_SETTING, "aa 03 01");
	g1_unit_reboot();

	upgradeAppLanguageInfoToFlash(0x03);
	zassert_equal(SettingStoreHandler(), -1);

	zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
	zassert_equal(g1_fake_flash_write_fake.call_count, 0);
	expect_flash(FLASH_SETTING, "aa 03 01");
}

ZTEST(persistence, test_startup_mode_above_2_is_rejected)
{
	upgradeDashboardStartupModeInfoToFlash(3);
	zassert_equal(SettingStoreHandler(), -1);

	expect_flash_fill(FLASH_SETTING, 3, 0xff);
	zassert_equal(get_dashboard_startup_mode(), 0);
}

ZTEST(persistence, test_same_startup_mode_is_not_rewritten)
{
	upgradeDashboardStartupModeInfoToFlash(0);
	zassert_equal(SettingStoreHandler(), -1);

	zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
	expect_flash_fill(FLASH_SETTING, 3, 0xff);
}

ZTEST(persistence, test_clean_commands_reset_one_field_each)
{
	put_flash(FLASH_SETTING, "aa 03 02");
	g1_unit_reboot();

	cleanAppLanguageInfo();
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa 02 02");
	zassert_equal(get_current_language(), 0x02);
	zassert_equal(get_dashboard_startup_mode(), 2);

	cleanDashBoardStartUpModeInfo();
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa 02 00");
	zassert_equal(get_dashboard_startup_mode(), 0);
}

/* DEFECT: getAppLanguageInfofromFlash() returns the erased byte (0xff) as the language. */
ZTEST_EXPECT_FAIL(persistence, test_startup_mode_stored_first_leaves_the_language_english);
ZTEST(persistence, test_startup_mode_stored_first_leaves_the_language_english)
{
	upgradeDashboardStartupModeInfoToFlash(1);
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa");
	expect_flash(FLASH_SETTING + 2, "01");

	g1_unit_reboot();

	zassert_equal(get_dashboard_startup_mode(), 1);
	zassert_equal(get_current_language(), 0x02, "language 0x%02x", get_current_language());
}

ZTEST(persistence, test_setting_record_without_magic_is_ignored)
{
	put_flash(FLASH_SETTING, "55 03 02");
	g1_unit_reboot();

	zassert_equal(get_current_language(), 0x02);
	zassert_equal(get_dashboard_startup_mode(), 0);
}

/* ---- user-settings setters ---- */

ZTEST(persistence, test_double_tap_action_change_requests_a_user_settings_commit)
{
	zassert_equal(upgradeDoubleTapCustomizeToFlash(3), 0);

	zassert_equal(g1_state()->double_tap_customize, 3);
	zassert_equal(storage_commit_request_fake.call_count, 1);
	zassert_equal(storage_commit_request_fake.arg0_val, 0);
	zassert_equal(storage_commit_request_fake.arg1_val, 0);
}

ZTEST(persistence, test_unchanged_or_invalid_double_tap_action_commits_nothing)
{
	g1_state()->double_tap_customize = 3;

	zassert_equal(upgradeDoubleTapCustomizeToFlash(3), 0);
	zassert_equal(upgradeDoubleTapCustomizeToFlash(6), -1);

	zassert_equal(g1_state()->double_tap_customize, 3);
	zassert_equal(storage_commit_request_fake.call_count, 0);
}

/* Double-tap action 4 needs startup mode 2. */
ZTEST(persistence, test_double_tap_action_4_and_startup_mode_follow_each_other)
{
	upgradeDoubleTapCustomizeToFlash(4);
	zassert_equal(storage_commit_request_fake.call_count, 1);
	zassert_equal(getSettingStorageQueueNum(), 1);
	run_flash_ops_thread();
	expect_flash(FLASH_SETTING, "aa");
	expect_flash(FLASH_SETTING + 2, "02");
	zassert_equal(get_dashboard_startup_mode(), 2);

	upgradeDashboardStartupModeInfoToFlash(0);
	run_flash_ops_thread();

	expect_flash(FLASH_SETTING + 2, "00");
	zassert_equal(g1_state()->double_tap_customize, 0);
	zassert_equal(storage_commit_request_fake.call_count, 2);
	zassert_equal(storage_commit_request_fake.arg0_history[1], 0);
	zassert_equal(storage_commit_request_fake.arg1_history[1], 0);
}

ZTEST(persistence, test_long_press_action_change_requests_a_user_settings_commit)
{
	zassert_equal(upgradeLongpressTapCustomizeToFlash(1), 0);
	zassert_equal(g1_state()->longpress_tap_customize, 1);
	zassert_equal(storage_commit_request_fake.call_count, 1);
	zassert_equal(storage_commit_request_fake.arg0_val, 0);
	zassert_equal(storage_commit_request_fake.arg1_val, 0);

	zassert_equal(upgradeLongpressTapCustomizeToFlash(1), 0); /* unchanged */
	zassert_equal(upgradeLongpressTapCustomizeToFlash(2), -1); /* only 0 / 1 */
	zassert_equal(storage_commit_request_fake.call_count, 1);
}
