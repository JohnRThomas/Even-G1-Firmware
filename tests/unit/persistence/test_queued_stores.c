/* Setting record: AA, language (02 English), startup mode (0 full, 1 dual, 2 minimal). */
#include "persistence.h"

#define SETTING_QUEUE_DEPTH 5

ZTEST(persistence, test_language_is_stored_as_aa_language_and_read_back_after_a_reboot) {
  zassert_equal(upgradeAppLanguageInfoToFlash(0x03), 0);
  run_flash_ops_thread();

  expect_flash(FLASH_SETTING, "aa 03");
  zassert_equal(get_current_language(), 0x03);

  g1_unit_reboot();
  zassert_equal(get_current_language(), 0x03);
  zassert_equal(get_dashboard_startup_mode(), 0);
}

ZTEST(persistence, test_lens_without_a_setting_record_is_english_with_startup_mode_0) {
  zassert_equal(getAppLanguageInfofromFlash(), 0x02);
  zassert_equal(getDashboardStartUpModeInfofromFlash(), 0);
}

ZTEST(persistence, test_language_and_startup_mode_share_the_record) {
  upgradeAppLanguageInfoToFlash(0x0e);
  upgradeDashboardStartupModeInfoToFlash(1);
  run_flash_ops_thread();

  expect_flash(FLASH_SETTING, "aa 0e 01");

  g1_unit_reboot();
  zassert_equal(get_current_language(), 0x0e);
  zassert_equal(get_dashboard_startup_mode(), 1);
}

ZTEST(persistence, test_each_queued_setting_gives_flash_ops_sem_once) {
  upgradeAppLanguageInfoToFlash(0x04);
  zassert_equal(k_sem_count_get(&flash_ops_sem), 1);
  upgradeDashboardStartupModeInfoToFlash(2);
  cleanAppLanguageInfo();
  cleanDashBoardStartUpModeInfo();
  zassert_equal(k_sem_count_get(&flash_ops_sem), 4);
  zassert_equal(getSettingStorageQueueNum(), 4);

  expect_flash_fill(FLASH_SETTING, 3, 0xff);
}

ZTEST(persistence, test_queued_settings_are_applied_in_order) {
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

ZTEST(persistence, test_setting_queue_holds_5_commands_and_rejects_the_6th) {
  for (int cmd = 0; cmd < SETTING_QUEUE_DEPTH; cmd++) {
    zassert_equal(upgradeAppLanguageInfoToFlash(0x01 + cmd), 0, "command %d", cmd);
  }

  zassert_equal(upgradeAppLanguageInfoToFlash(0x07), -1);
  zassert_equal(cleanAppLanguageInfo(), -1);
  zassert_equal(upgradeDashboardStartupModeInfoToFlash(1), -1);
  zassert_equal(cleanDashBoardStartUpModeInfo(), -1);
  zassert_equal(k_sem_count_get(&flash_ops_sem), SETTING_QUEUE_DEPTH);

  run_flash_ops_thread();
  expect_flash(FLASH_SETTING, "aa 05");
}

ZTEST(persistence, test_language_0_is_rejected) {
  put_flash(FLASH_SETTING, "aa 03 01");
  g1_unit_reboot();

  upgradeAppLanguageInfoToFlash(0x00);
  zassert_equal(SettingStoreHandler(), -1);

  expect_flash(FLASH_SETTING, "aa 03 01");
  zassert_equal(get_current_language(), 0x03);
  zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
}

ZTEST(persistence, test_same_language_is_not_rewritten) {
  put_flash(FLASH_SETTING, "aa 03 01");
  g1_unit_reboot();

  upgradeAppLanguageInfoToFlash(0x03);
  zassert_equal(SettingStoreHandler(), -1);

  zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
  zassert_equal(g1_fake_flash_write_fake.call_count, 0);
  expect_flash(FLASH_SETTING, "aa 03 01");
}

ZTEST(persistence, test_startup_mode_above_2_is_rejected) {
  upgradeDashboardStartupModeInfoToFlash(3);
  zassert_equal(SettingStoreHandler(), -1);

  expect_flash_fill(FLASH_SETTING, 3, 0xff);
  zassert_equal(get_dashboard_startup_mode(), 0);
}

ZTEST(persistence, test_same_startup_mode_is_not_rewritten) {
  upgradeDashboardStartupModeInfoToFlash(0);
  zassert_equal(SettingStoreHandler(), -1);

  zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
  expect_flash_fill(FLASH_SETTING, 3, 0xff);
}

ZTEST(persistence, test_clean_commands_reset_one_field_each) {
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

/* the language byte stays erased (ff) */
ZTEST(persistence, test_startup_mode_stored_first_leaves_the_language_english) {
  upgradeDashboardStartupModeInfoToFlash(1);
  run_flash_ops_thread();
  expect_flash(FLASH_SETTING, "aa");
  expect_flash(FLASH_SETTING + 2, "01");

  g1_unit_reboot();

  zassert_equal(get_dashboard_startup_mode(), 1);
  zassert_equal(get_current_language(), 0x02, "language 0x%02x", get_current_language());
}

ZTEST(persistence, test_setting_record_without_magic_is_ignored) {
  put_flash(FLASH_SETTING, "55 03 02");
  g1_unit_reboot();

  zassert_equal(get_current_language(), 0x02);
  zassert_equal(get_dashboard_startup_mode(), 0);
}

/* ---- user-settings setters ---- */

ZTEST(persistence, test_double_tap_action_change_requests_a_user_settings_commit) {
  zassert_equal(upgradeDoubleTapCustomizeToFlash(3), 0);

  zassert_equal(g1_state()->double_tap_customize, 3);
  zassert_equal(storage_commit_request_fake.call_count, 1);
  zassert_equal(storage_commit_request_fake.arg0_val, 0);
  zassert_equal(storage_commit_request_fake.arg1_val, 0);
}

ZTEST(persistence, test_unchanged_or_invalid_double_tap_action_commits_nothing) {
  g1_state()->double_tap_customize = 3;

  zassert_equal(upgradeDoubleTapCustomizeToFlash(3), 0);
  zassert_equal(upgradeDoubleTapCustomizeToFlash(6), -1);

  zassert_equal(g1_state()->double_tap_customize, 3);
  zassert_equal(storage_commit_request_fake.call_count, 0);
}

/* Double-tap action 4 needs startup mode 2. */
ZTEST(persistence, test_double_tap_action_4_and_startup_mode_follow_each_other) {
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

ZTEST(persistence, test_long_press_action_change_requests_a_user_settings_commit) {
  zassert_equal(upgradeLongpressTapCustomizeToFlash(1), 0);
  zassert_equal(g1_state()->longpress_tap_customize, 1);
  zassert_equal(storage_commit_request_fake.call_count, 1);
  zassert_equal(storage_commit_request_fake.arg0_val, 0);
  zassert_equal(storage_commit_request_fake.arg1_val, 0);

  zassert_equal(upgradeLongpressTapCustomizeToFlash(1), 0); /* unchanged */
  zassert_equal(upgradeLongpressTapCustomizeToFlash(2), -1); /* only 0 / 1 */
  zassert_equal(storage_commit_request_fake.call_count, 1);
}

/* Quick note slot: AA, title 0x29, text 0x119. */

#define NOTE_TITLE_LEN        0x29
#define NOTE_TEXT_LEN         0x119
#define QUICKNOTE_QUEUE_DEPTH 20

static uint32_t slot(int index) {
  return FLASH_QUICKNOTES + index * QUICKNOTE_LEN;
}

static void note(int index, const char *title, const char *text) {
  struct quicknote_item *item = &g1_state()->quicknote->notes[index];

  strncpy(item->title, title, sizeof(item->title));
  strncpy(item->text, text, sizeof(item->text));
}

ZTEST(persistence, test_quick_note_is_stored_as_aa_title_text_in_its_slot) {
  note(1, "Shopping", "milk, eggs");

  zassert_equal(upgradeQuickNoteDataToFlash(1), 0);
  run_flash_ops_thread();

  expect_flash(slot(1), "aa  53 68 6f 70 70 69 6e 67 00"); /* "Shopping" */
  expect_flash_fill(slot(1) + 1 + 8, NOTE_TITLE_LEN - 8, 0x00);
  expect_flash(slot(1) + 0x2a, "6d 69 6c 6b 2c 20 65 67 67 73 00"); /* "milk, eggs" */
  expect_flash_fill(slot(1) + 0x2a + 10, NOTE_TEXT_LEN - 10, 0x00);
  expect_flash_fill(slot(0), QUICKNOTE_LEN, 0xff);
  expect_flash_fill(slot(2), 2 * QUICKNOTE_LEN, 0xff);
}

ZTEST(persistence, test_quick_note_is_read_back_after_a_reboot) {
  char title[NOTE_TITLE_LEN];
  char text[NOTE_TEXT_LEN];

  note(2, "Call Anna", "about the trip on Friday");
  upgradeQuickNoteDataToFlash(2);
  run_flash_ops_thread();

  g1_unit_reboot();

  zassert_equal(getQuickNoteDataFromFlash(2, title, sizeof(title), text, sizeof(text)), 0);
  expect_str(title, "Call Anna");
  expect_str(text, "about the trip on Friday");
}

ZTEST(persistence, test_empty_quick_note_slot_reads_as_missing) {
  char title[NOTE_TITLE_LEN] = "unchanged";
  char text[NOTE_TEXT_LEN] = "unchanged";

  zassert_equal(getQuickNoteDataFromFlash(0, title, sizeof(title), text, sizeof(text)), -1);
  expect_str(title, "unchanged");
}

ZTEST(persistence, test_deleted_quick_note_slot_is_erased_and_the_others_kept) {
  char title[NOTE_TITLE_LEN];
  char text[NOTE_TEXT_LEN];

  note(0, "A", "first");
  note(3, "D", "fourth");
  upgradeQuickNoteDataToFlash(0);
  upgradeQuickNoteDataToFlash(3);
  run_flash_ops_thread();

  zassert_equal(deleteQuickNoteData(0), 0);
  run_flash_ops_thread();

  expect_flash_fill(slot(0), QUICKNOTE_LEN, 0xff);
  zassert_equal(getQuickNoteDataFromFlash(0, title, sizeof(title), text, sizeof(text)), -1);
  zassert_equal(getQuickNoteDataFromFlash(3, title, sizeof(title), text, sizeof(text)), 0);
  expect_str(text, "fourth");
}

ZTEST(persistence, test_queued_quick_note_commands_are_applied_in_order) {
  note(0, "A", "first");
  note(1, "B", "second");
  note(2, "C", "third");

  upgradeQuickNoteDataToFlash(0);
  upgradeQuickNoteDataToFlash(1);
  deleteQuickNoteData(0);
  deleteQuickNoteData(2);
  upgradeQuickNoteDataToFlash(2);
  zassert_equal(getQuickNoteQueueNum(), 5);
  run_flash_ops_thread();

  expect_flash_fill(slot(0), QUICKNOTE_LEN, 0xff);
  expect_flash(slot(1), "aa 42 00");
  expect_flash(slot(2), "aa 43 00");
  zassert_equal(getQuickNoteQueueNum(), 0);
}

ZTEST(persistence, test_each_queued_quick_note_command_gives_flash_ops_sem_once) {
  upgradeQuickNoteDataToFlash(0);
  deleteQuickNoteData(1);
  upgradeQuickNoteDataToFlash(2);

  zassert_equal(k_sem_count_get(&flash_ops_sem), 3);
  zassert_equal(run_flash_ops_thread(), 3);
  zassert_equal(k_sem_count_get(&flash_ops_sem), 0);
}

ZTEST(persistence, test_quick_note_queue_holds_20_commands_and_rejects_the_21st) {
  for (int cmd = 0; cmd < QUICKNOTE_QUEUE_DEPTH; cmd++) {
    zassert_equal(upgradeQuickNoteDataToFlash(cmd % 4), 0, "command %d", cmd);
  }

  zassert_equal(upgradeQuickNoteDataToFlash(0), -1);
  zassert_equal(deleteQuickNoteData(0), -1);
  zassert_equal(getQuickNoteQueueNum(), QUICKNOTE_QUEUE_DEPTH);
}

ZTEST(persistence, test_unchanged_quick_note_is_not_rewritten) {
  note(1, "Shopping", "milk, eggs");
  upgradeQuickNoteDataToFlash(1);
  run_flash_ops_thread();
  zassert_equal(g1_fake_flash_erase_fake.call_count, 1);

  upgradeQuickNoteDataToFlash(1);
  run_flash_ops_thread();
  zassert_equal(g1_fake_flash_erase_fake.call_count, 1, "erased again");

  note(1, "Shopping", "milk, eggs, bread");
  upgradeQuickNoteDataToFlash(1);
  run_flash_ops_thread();
  zassert_equal(g1_fake_flash_erase_fake.call_count, 2, "change not written");
  expect_flash(slot(1) + 0x2a + 10, "2c 20 62 72 65 61 64 00"); /* ", bread" */
}

ZTEST(persistence, test_quick_note_index_4_is_rejected) {
  char title[NOTE_TITLE_LEN];
  char text[NOTE_TEXT_LEN];

  upgradeQuickNoteDataToFlash(4);
  zassert_equal(QuickNoteStoreHandler(), (unsigned int)-1);
  deleteQuickNoteData(4);
  zassert_equal(QuickNoteStoreHandler(), (unsigned int)-1);

  zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
  zassert_equal(g1_fake_flash_write_fake.call_count, 0);
  zassert_equal(getQuickNoteDataFromFlash(4, title, sizeof(title), text, sizeof(text)), -1);
}

ZTEST(persistence, test_quick_note_title_of_0x29_bytes_is_stored_whole) {
  char title[NOTE_TITLE_LEN + 1];

  memset(title, 'T', NOTE_TITLE_LEN);
  title[NOTE_TITLE_LEN] = '\0';
  note(0, title, "x");
  upgradeQuickNoteDataToFlash(0);
  run_flash_ops_thread();

  expect_flash_fill(slot(0) + 1, NOTE_TITLE_LEN, 'T');
  expect_flash(slot(0) + 0x2a, "78 00");
}

ZTEST(persistence, test_quick_note_read_copies_at_most_the_field_sizes) {
  char title[0x40];
  char text[0x140];

  note(0, "Title", "Text");
  upgradeQuickNoteDataToFlash(0);
  run_flash_ops_thread();
  memset(title, '~', sizeof(title));
  memset(text, '~', sizeof(text));

  zassert_equal(getQuickNoteDataFromFlash(0, title, sizeof(title), text, sizeof(text)), 0);

  expect_str(title, "Title");
  expect_fill(&title[NOTE_TITLE_LEN], sizeof(title) - NOTE_TITLE_LEN, '~', "title tail");
  expect_fill(&text[NOTE_TEXT_LEN], sizeof(text) - NOTE_TEXT_LEN, '~', "text tail");
}
