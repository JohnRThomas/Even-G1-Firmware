/* Quick note slot: AA, title 0x29, text 0x119. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_storage.h"
#include "persistence.h"
#include "storage/storage.h"

#define NOTE_TITLE_LEN 0x29
#define NOTE_TEXT_LEN  0x119
#define QUICKNOTE_QUEUE_DEPTH 20

static uint32_t slot(int i)
{
	return FLASH_QUICKNOTES + i * QUICKNOTE_LEN;
}

static void note(int i, const char *title, const char *text)
{
	struct quicknote_item *n = &g1_state()->quicknote->notes[i];

	memset(n->title, 0, sizeof(n->title));
	memset(n->text, 0, sizeof(n->text));
	strncpy(n->title, title, sizeof(n->title));
	strncpy(n->text, text, sizeof(n->text));
}

ZTEST(persistence, test_quick_note_is_stored_as_aa_title_text_in_its_slot)
{
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

ZTEST(persistence, test_quick_note_is_read_back_after_a_reboot)
{
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

ZTEST(persistence, test_empty_quick_note_slot_reads_as_missing)
{
	char title[NOTE_TITLE_LEN] = "unchanged";
	char text[NOTE_TEXT_LEN] = "unchanged";

	zassert_equal(getQuickNoteDataFromFlash(0, title, sizeof(title), text, sizeof(text)), -1);
	expect_str(title, "unchanged");
}

ZTEST(persistence, test_deleted_quick_note_slot_is_erased_and_the_others_kept)
{
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

ZTEST(persistence, test_queued_quick_note_commands_are_applied_in_order)
{
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

ZTEST(persistence, test_each_queued_quick_note_command_gives_flash_ops_sem_once)
{
	upgradeQuickNoteDataToFlash(0);
	deleteQuickNoteData(1);
	upgradeQuickNoteDataToFlash(2);

	zassert_equal(k_sem_count_get(&flash_ops_sem), 3);
	zassert_equal(run_flash_ops_thread(), 3);
	zassert_equal(k_sem_count_get(&flash_ops_sem), 0);
}

ZTEST(persistence, test_quick_note_queue_holds_20_commands_and_rejects_the_21st)
{
	for (int i = 0; i < QUICKNOTE_QUEUE_DEPTH; i++) {
		zassert_equal(upgradeQuickNoteDataToFlash(i % 4), 0, "command %d", i);
	}

	zassert_equal(upgradeQuickNoteDataToFlash(0), -1);
	zassert_equal(deleteQuickNoteData(0), -1);
	zassert_equal(getQuickNoteQueueNum(), QUICKNOTE_QUEUE_DEPTH);
}

ZTEST(persistence, test_unchanged_quick_note_is_not_rewritten)
{
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

ZTEST(persistence, test_quick_note_index_4_is_rejected)
{
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

ZTEST(persistence, test_quick_note_title_of_0x29_bytes_is_stored_whole)
{
	char title[NOTE_TITLE_LEN + 1];

	memset(title, 'T', NOTE_TITLE_LEN);
	title[NOTE_TITLE_LEN] = '\0';
	note(0, title, "x");
	upgradeQuickNoteDataToFlash(0);
	run_flash_ops_thread();

	expect_flash_fill(slot(0) + 1, NOTE_TITLE_LEN, 'T');
	expect_flash(slot(0) + 0x2a, "78 00");
}

ZTEST(persistence, test_quick_note_read_copies_at_most_the_field_sizes)
{
	char title[0x40];
	char text[0x140];

	note(0, "Title", "Text");
	upgradeQuickNoteDataToFlash(0);
	run_flash_ops_thread();
	memset(title, '~', sizeof(title));
	memset(text, '~', sizeof(text));

	zassert_equal(getQuickNoteDataFromFlash(0, title, sizeof(title), text, sizeof(text)), 0);

	expect_str(title, "Title");
	for (size_t i = NOTE_TITLE_LEN; i < sizeof(title); i++) {
		zassert_equal(title[i], '~', "title[0x%x] written", (unsigned int)i);
	}
	for (size_t i = NOTE_TEXT_LEN; i < sizeof(text); i++) {
		zassert_equal(text[i], '~', "text[0x%x] written", (unsigned int)i);
	}
}
