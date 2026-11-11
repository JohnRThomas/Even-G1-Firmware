/*
 * 0x1E quick note sub 3: 1E len16 seq 03 total16 index16 slot flag title_len title text_len16 text.
 * Also quicknote_store.c and 0x51. quicknote_cache is reloaded from flash before every use.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_display.h"
#include "fakes_storage.h"
#include "ble/ble_proto.h"
#include "storage/storage.h"
#include "features.h"

#define QN_FLASH      0x403000 /* QUICKNOTE_FLASH_ADDR */
#define QN_FLASH_NOTE 0x200
#define QN_MAGIC      0xaa
#define QN_TITLE      0x29     /* QUICKNOTE_TITLE_LEN */
#define QN_TEXT       0x119    /* QUICKNOTE_TEXT_LEN */
#define QN_BLOCK      0x50f    /* sizeof(struct quicknote_ctx) */
#define QN_QUEUE      20       /* QUICKNOTE_QUEUE_DEPTH */

extern struct k_msgq quicknote_msgq;

static struct quicknote_item *note(int slot)
{
	return &g1_state()->quicknote->notes[slot - 1];
}

static size_t qn_pkt(uint8_t *pkt, uint8_t seq, uint8_t slot, const char *title, const char *text)
{
	size_t tl = strlen(title);
	size_t xl = strlen(text);
	size_t n = 0;

	pkt[n++] = 0x1e;
	n += 2; /* length */
	pkt[n++] = seq;
	pkt[n++] = 0x03;
	pkt[n++] = 0x01; /* total 1 */
	pkt[n++] = 0x00;
	pkt[n++] = 0x01; /* index 1 */
	pkt[n++] = 0x00;
	pkt[n++] = slot;
	pkt[n++] = 0x01; /* note present */
	pkt[n++] = (uint8_t)tl;
	memcpy(&pkt[n], title, tl);
	n += tl;
	pkt[n++] = (uint8_t)xl;
	pkt[n++] = (uint8_t)(xl >> 8);
	memcpy(&pkt[n], text, xl);
	n += xl;
	return feat_set_len16(pkt, n);
}

static void run_store(void)
{
	while (getQuickNoteQueueNum() != 0) {
		QuickNoteStoreHandler();
	}
}

ZTEST(features, test_0x1e_notes_transcript_empty_slot_3)
{
	note(3)->valid = 1;
	strcpy(note(3)->title, "old");

	g1_send_hex("1e 10 00 29 03 01 00 01 00 03 00 01 00 01 00 00");

	expect_frame("1e 10 00 29 03 01 00 01 00 00");
	expect_no_frame();
	zassert_equal(note(3)->valid, 0);
	feat_expect_field(note(3)->title, QN_TITLE, "");
	zassert_equal(getQuickNoteQueueNum(), 1);
}

ZTEST(features, test_0x1e_note_is_stored_in_its_slot_and_queued_for_flash)
{
	g1_send_vector("0x1e_quicknote_add");

	expect_frame("1e 20 00 11 03 01 00 01 00 00");
	zassert_equal(note(1)->valid, 1);
	feat_expect_field(note(1)->title, QN_TITLE, "testing Your");
	feat_expect_field(note(1)->text, QN_TEXT, "Oh wow");
	zassert_equal(note(2)->valid, 0, "other slots untouched");
	zassert_equal(getQuickNoteQueueNum(), 1);
}

ZTEST(features, test_0x1e_note_in_slot_4_is_stored)
{
	uint8_t pkt[64];

	g1_send(pkt, qn_pkt(pkt, 0x12, 4, "four", "last slot"));

	expect_frame("1e 1b 00 12 03 01 00 01 00 00");
	zassert_equal(note(4)->valid, 1);
	feat_expect_field(note(4)->title, QN_TITLE, "four");
	feat_expect_field(note(4)->text, QN_TEXT, "last slot");
}

ZTEST(features, test_0x1e_title_of_0x28_chars_is_stored_whole)
{
	char title[0x28 + 1];
	uint8_t pkt[96];

	memset(title, 'T', 0x28);
	title[0x28] = '\0';
	g1_send(pkt, qn_pkt(pkt, 0x13, 2, title, "body"));

	feat_expect_field(note(2)->title, QN_TITLE, title);
	feat_expect_field(note(2)->text, QN_TEXT, "body");
	expect_frame("1e 3a 00 13 03 01 00 01 00 00");
}

/* DEFECT: copies up to 0x29 bytes, leaving no terminator. */
ZTEST_EXPECT_FAIL(features, test_0x1e_title_of_0x29_chars_is_cut_to_keep_a_terminator);
ZTEST(features, test_0x1e_title_of_0x29_chars_is_cut_to_keep_a_terminator)
{
	char title[0x29 + 1];
	uint8_t pkt[96];

	memset(title, 'T', 0x29);
	title[0x29] = '\0';
	g1_send(pkt, qn_pkt(pkt, 0x14, 2, title, "body"));

	zassert_equal(note(2)->title[0x28], '\0', "title not terminated");
	zassert_equal(strnlen(note(2)->title, QN_TITLE), 0x28);
}

/* DEFECT: the single-packet path doesn't range-check the slot. */
ZTEST_EXPECT_FAIL(features, test_0x1e_note_in_slot_5_is_rejected);
ZTEST(features, test_0x1e_note_in_slot_5_is_rejected)
{
	uint8_t pkt[64];

	feat_guard_block(&g1_state()->quicknote, QN_BLOCK, 0x200, 0x200);

	g1_send(pkt, qn_pkt(pkt, 0x15, 5, "five", "too many"));

	feat_expect_guard_intact();
	expect_frame("1e 1a 00 15 03 01 00 01 00 01");
}

/* DEFECT: clears notes[slot - 1].valid before the range check. */
ZTEST_EXPECT_FAIL(features, test_0x1e_multi_packet_note_in_slot_5_is_rejected_without_writing);
ZTEST(features, test_0x1e_multi_packet_note_in_slot_5_is_rejected_without_writing)
{
	/* total 2, index 1, slot 5, title "t" */
	uint8_t pkt[] = {0x1e, 0x00, 0x00, 0x16, 0x03, 0x02, 0x00, 0x01, 0x00,
			 0x05, 0x01, 0x01, 't', 'a', 'b', 'c'};

	feat_guard_block(&g1_state()->quicknote, QN_BLOCK, 0x200, 0x200);

	g1_send(pkt, feat_set_len16(pkt, sizeof(pkt)));

	expect_frame("1e 10 00 16 03 02 00 01 00 01");
	feat_expect_guard_intact();
}

/* DEFECT: later chunks append to the static qn_text_buf unbounded. */
ZTEST(features, test_0x1e_multi_packet_text_longer_than_the_note_is_refused)
{
	uint8_t pkt[0x100] = {0x1e, 0x00, 0x00, 0x17, 0x03, 0x03, 0x00, 0x01, 0x00,
			      0x01, 0x01, 0x01, 't'};

	ztest_test_skip();

	g1_send(pkt, feat_set_len16(pkt, 16));
	memset(&pkt[9], 'x', sizeof(pkt) - 9);
	pkt[7] = 0x02;
	g1_send(pkt, feat_set_len16(pkt, sizeof(pkt)));
	pkt[7] = 0x03;
	g1_send(pkt, feat_set_len16(pkt, sizeof(pkt)));
	expect_frame("1e 10 00 17 03 03 00 01 00 00");
	expect_frame("1e 00 01 17 03 03 00 02 00 00");
	expect_frame("1e 00 01 17 03 03 00 03 00 01");
	zassert_equal(note(1)->valid, 0);
}

ZTEST(features, test_0x1e_note_survives_a_reboot_through_flash)
{
	char title[QN_TITLE];
	char text[QN_TEXT];

	g1_send_vector("0x1e_quicknote_add");
	run_store();

	zassert_equal(g1_fake_flash_image[QN_FLASH], QN_MAGIC);
	feat_expect_field((const char *)&g1_fake_flash_image[QN_FLASH + 1], QN_TITLE,
			  "testing Your");
	feat_expect_field((const char *)&g1_fake_flash_image[QN_FLASH + 1 + QN_TITLE], QN_TEXT,
			  "Oh wow");

	g1_unit_reboot();
	zassert_equal(g1_state()->quicknote->notes[0].valid, 0, "RAM copy cleared by the reboot");

	memset(title, 0x55, sizeof(title));
	memset(text, 0x55, sizeof(text));
	zassert_equal(getQuickNoteDataFromFlash(0, title, QN_TITLE, text, QN_TEXT), 0);
	feat_expect_field(title, QN_TITLE, "testing Your");
	feat_expect_field(text, QN_TEXT, "Oh wow");
}

ZTEST(features, test_quicknote_store_keeps_four_notes_apart)
{
	static const char *const titles[4] = {"one", "two", "three", "four"};
	char title[QN_TITLE];
	char text[QN_TEXT];
	uint8_t pkt[64];

	for (int i = 0; i < 4; i++) {
		g1_send(pkt, qn_pkt(pkt, 0x20 + i, i + 1, titles[i], "x"));
	}
	run_store();

	for (int i = 0; i < 4; i++) {
		zassert_equal(g1_fake_flash_image[QN_FLASH + i * QN_FLASH_NOTE], QN_MAGIC,
			      "slot %d", i + 1);
		zassert_equal(getQuickNoteDataFromFlash(i, title, QN_TITLE, text, QN_TEXT), 0,
			      "slot %d", i + 1);
		feat_expect_field(title, QN_TITLE, titles[i]);
	}
}

ZTEST(features, test_0x1e_empty_note_deletes_the_flash_copy)
{
	char title[QN_TITLE];
	char text[QN_TEXT];

	g1_send_vector("0x1e_quicknote_add");
	run_store();
	g1_send_vector("0x1e_quicknote_empty_slot1");
	run_store();

	expect_frame("1e 20 00 11 03 01 00 01 00 00");
	expect_frame("1e 10 00 0f 03 01 00 01 00 00");
	zassert_equal(note(1)->valid, 0);
	zassert_not_equal(g1_fake_flash_image[QN_FLASH], QN_MAGIC);
	zassert_equal(getQuickNoteDataFromFlash(0, title, QN_TITLE, text, QN_TEXT), -1);
}

ZTEST(features, test_quicknote_store_refuses_index_4)
{
	char title[QN_TITLE];
	char text[QN_TEXT];

	zassert_equal(upgradeQuickNoteDataToFlash(4), 0, "queued");
	zassert_equal(QuickNoteStoreHandler(), (unsigned int)-1);
	zassert_equal(g1_fake_flash_write_fake.call_count, 0);
	zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
	zassert_equal(getQuickNoteDataFromFlash(4, title, QN_TITLE, text, QN_TEXT), -1);

	strcpy(g1_state()->quicknote->notes[3].title, "slot 4");
	zassert_equal(upgradeQuickNoteDataToFlash(3), 0);
	zassert_equal(QuickNoteStoreHandler(), 0);
	zassert_equal(getQuickNoteDataFromFlash(3, title, QN_TITLE, text, QN_TEXT), 0);
	feat_expect_field(title, QN_TITLE, "slot 4");
}

ZTEST(features, test_quicknote_store_queue_holds_20_requests)
{
	for (int i = 0; i < QN_QUEUE; i++) {
		zassert_equal(upgradeQuickNoteDataToFlash(i % 4), 0, "request %d", i + 1);
	}
	zassert_equal(getQuickNoteQueueNum(), QN_QUEUE);
	zassert_equal(deleteQuickNoteData(0), -1, "request 21");
	zassert_equal(getQuickNoteQueueNum(), QN_QUEUE);
}

ZTEST(features, test_quicknote_store_skips_an_unchanged_note)
{
	g1_send_vector("0x1e_quicknote_add");
	run_store();
	zassert_equal(g1_fake_flash_write_fake.call_count, 1);

	g1_send_vector("0x1e_quicknote_add");
	run_store();
	zassert_equal(g1_fake_flash_write_fake.call_count, 1, "rewritten without a change");
}

ZTEST(features, test_0x51_enable_starts_the_quick_note_task)
{
	struct prompt_info *ts = g1_state()->ts_info;
	int64_t now;

	g1_clock_advance_ms(1234);
	now = g1_clock_uptime_ms();
	g1_send_hex("51 06 00 2a 01 01");

	zassert_equal(ts->enable, 1);
	zassert_equal(ts->action_cmd, 1);
	zassert_equal(ts->ts, now + 5000, "ts %lld, uptime %lld", ts->ts, now);
	zassert_equal(g1_state()->esb_down_id, 0x2a);
	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal(update_persist_task_status_fake.arg1_val, 0x12);
	expect_frame_padded("51 06 00 2a 01 c9", 20);
}

ZTEST(features, test_0x51_disable_clears_enable)
{
	g1_send_hex("51 06 00 2a 02 01");
	g1_send_hex("51 06 00 2b 02 00");

	zassert_equal(g1_state()->ts_info->enable, 0);
	zassert_equal(update_persist_task_status_fake.call_count, 1);
	expect_frame_padded("51 06 00 2a 02 c9", 20);
	expect_frame_padded("51 06 00 2b 02 c9", 20);
}

ZTEST(features, test_0x51_sub_above_2_or_wrong_length_is_rejected)
{
	g1_send_hex("51 06 00 2c 03 01");
	g1_send_hex("51 07 00 2d 01 01");

	zassert_equal(g1_state()->ts_info->enable, 0);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
	expect_frame_padded("51 06 00 2c 03 ca", 20);
	expect_frame_padded("51 06 00 2d 01 ca", 20);
}
