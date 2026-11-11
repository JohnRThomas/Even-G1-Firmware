/*
 * 0x52 transcribe control and 0x53 heartbeat. 0x52 sub 1 mode: 0 stop, 1 start, 2 start without
 * clock and icon, 3 pause, 4 resume; sub 2 text; sub 3 status. flags bit 0 animates the icon.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "fakes_esb.h"
#include "ble/ble_proto.h"
#include "features.h"

#define TR_HDR       12    /* sub 2 header */
#define TR_TEXT      0x200 /* sizeof(transcribe_buf.data) */
#define TR_BLOCK     0x208 /* allocated block size */
#define TR_CHUNK_MAX (0x100 - TR_HDR) /* largest packet the link delivers */

static struct transcribe_buf *tr(void)
{
	return g1_state()->transcribe_buf;
}

/* the state sub 1 mode 1 / 2 leaves */
static void tr_running(uint8_t mode)
{
	tr()->running = true;
	tr()->mode = mode;
	tr()->flags = mode == 2 ? 0x00 : 0xff;
}

static size_t tr_text(uint8_t *pkt, uint8_t seq, uint16_t total, uint16_t idx, uint8_t status,
		      uint8_t icon_off, uint8_t dark, const void *text, size_t n)
{
	pkt[0] = 0x52;
	pkt[3] = seq;
	pkt[4] = 0x02;
	pkt[5] = (uint8_t)total;
	pkt[6] = (uint8_t)(total >> 8);
	pkt[7] = (uint8_t)idx;
	pkt[8] = (uint8_t)(idx >> 8);
	pkt[9] = status;
	pkt[10] = icon_off;
	pkt[11] = dark;
	memcpy(&pkt[TR_HDR], text, n);
	return feat_set_len16(pkt, TR_HDR + n);
}

static void tr_send_str(uint8_t seq, uint16_t total, uint16_t idx, const char *text)
{
	uint8_t pkt[0x100];

	g1_send(pkt, tr_text(pkt, seq, total, idx, 0, 0, 0, text, strlen(text)));
}

ZTEST(features, test_0x52_start_mode_2_runs_with_the_flags_clear)
{
	g1_send_hex("52 06 00 05 01 02");

	zassert_true(tr()->running);
	zassert_equal(tr()->mode, 2);
	zassert_equal(tr()->flags, 0x00);
}

ZTEST(features, test_0x52_start_sets_the_esb_down_id)
{
	g1_send_hex("52 06 00 05 01 01");

	zassert_equal(g1_state()->esb_down_id, 0x05);
}

ZTEST(features, test_0x52_start_clears_the_previous_text)
{
	static const uint8_t zero[TR_TEXT];

	memcpy(tr()->data, "old text", 8);
	tr()->len = 8;
	tr()->status = 3;
	tr()->dark_lines = 4;

	g1_send_hex("52 06 00 05 01 01");

	zassert_equal(tr()->len, 0);
	zassert_equal(tr()->status, 0);
	zassert_equal(tr()->dark_lines, 0);
	zassert_mem_equal(tr()->data, zero, TR_TEXT, "text not cleared");
}

ZTEST(features, test_0x52_resume_in_mode_2_leaves_the_icon_flag_clear)
{
	tr_running(2);

	g1_send_hex("52 06 00 08 01 04");

	zassert_equal(dmic_stream_pause_fake.arg0_val, 0);
	zassert_equal(tr()->flags, 0x00);
}

/* the reply to an unknown mode is unspecified; only its presence is checked */
ZTEST(features, test_0x52_unknown_mode_changes_nothing)
{
	tr_running(1);

	g1_send_hex("52 06 00 09 01 05");

	zassert_true(tr()->running);
	zassert_equal(tr()->mode, 1);
	zassert_equal(tr()->flags, 0xff);
	zassert_equal(dmic_stream_stop_fake.call_count, 0);
	zassert_equal(dmic_stream_pause_fake.call_count, 0);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
	zassert_equal(g1_frame_count(), 1, "one reply");
}

ZTEST(features, test_0x52_text_in_one_packet_is_stored)
{
	uint8_t pkt[64];

	tr_running(1);
	g1_send(pkt, tr_text(pkt, 0x10, 1, 1, 0x02, 0x00, 0x03, "Hello world", 11));

	feat_expect_field((const char *)tr()->data, TR_TEXT, "Hello world");
	zassert_equal(tr()->len, 11);
	zassert_equal(tr()->status, 0x02);
	zassert_equal(tr()->dark_lines, 0x03);
	expect_frame("52 0a 00 10 02 01 00 01 00 c9");
	expect_no_frame();
}

ZTEST(features, test_0x52_text_sets_the_esb_down_id)
{
	tr_running(1);
	g1_state()->esb_down_recving = 1;

	tr_send_str(0x11, 1, 1, "abc");

	zassert_equal(g1_state()->esb_down_id, 0x11);
	zassert_equal(g1_state()->esb_down_recving, 0);
}

ZTEST(features, test_0x52_text_byte_10_01_clears_the_icon_flag)
{
	uint8_t pkt[64];

	tr_running(1);
	g1_send(pkt, tr_text(pkt, 0x12, 1, 1, 0, 0x01, 0, "abc", 3));

	zassert_equal(tr()->flags, 0xfe);
}

ZTEST(features, test_0x52_text_byte_10_00_sets_the_icon_flag)
{
	uint8_t pkt[64];

	tr_running(1);
	tr()->flags = 0xfe;
	g1_send(pkt, tr_text(pkt, 0x12, 1, 1, 0, 0x00, 0, "abc", 3));

	zassert_equal(tr()->flags, 0xff);
}

ZTEST(features, test_0x52_text_in_mode_2_keeps_the_icon_flag_clear)
{
	uint8_t pkt[64];

	tr_running(2);
	g1_send(pkt, tr_text(pkt, 0x12, 1, 1, 0, 0x00, 0, "abc", 3));

	zassert_equal(tr()->flags, 0x00);
}

ZTEST(features, test_0x52_text_of_only_the_header_stores_an_empty_text)
{
	tr_running(1);
	tr_send_str(0x15, 1, 1, "old");
	g1_frames_clear();

	g1_send_hex("52 0c 00 16 02 01 00 01 00 00 00 00");

	zassert_equal(tr()->len, 0);
	feat_expect_field((const char *)tr()->data, TR_TEXT, "");
	expect_frame("52 0a 00 16 02 01 00 01 00 c9");
}

/* DEFECT: len = data_len - 12 underflows to 0xffff and that many bytes are copied. */
ZTEST(features, test_0x52_text_shorter_than_its_header_is_refused)
{
	ztest_test_skip();

	tr_running(1);
	g1_send_hex("52 0b 00 17 02 01 00 01 00 00 00");

	expect_frame("52 0a 00 17 02 01 00 01 00 ca");
	zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_in_two_chunks_is_assembled)
{
	uint8_t pkt[64];

	tr_running(1);
	g1_send(pkt, tr_text(pkt, 0x20, 2, 1, 0x01, 0x00, 0x01, "Hello ", 6));
	expect_frame("52 0a 00 20 02 02 00 01 00 c9");
	zassert_equal(tr()->len, 0, "text shown before the last chunk");

	g1_send(pkt, tr_text(pkt, 0x21, 2, 2, 0x04, 0x01, 0x05, "world", 5));
	expect_frame("52 0a 00 21 02 02 00 02 00 c9");

	feat_expect_field((const char *)tr()->data, TR_TEXT, "Hello world");
	zassert_equal(tr()->len, 11);
	zassert_equal(tr()->status, 0x04);
	zassert_equal(tr()->dark_lines, 0x05);
	zassert_equal(tr()->flags, 0xfe);
	zassert_equal(g1_state()->esb_down_id, 0x21);
}

/* 244 + 244 + 24 = 0x200 */
ZTEST(features, test_0x52_text_of_0x200_bytes_is_accepted)
{
	static uint8_t text[TR_TEXT];
	uint8_t pkt[0x100];

	for (size_t i = 0; i < sizeof(text); i++) {
		text[i] = 'a' + i % 26;
	}
	feat_guard_block(&g1_state()->transcribe_buf, TR_BLOCK, 0, 0x40);
	tr_running(1);

	g1_send(pkt, tr_text(pkt, 0x30, 3, 1, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x31, 3, 2, 0, 0, 0, text + TR_CHUNK_MAX, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x32, 3, 3, 0, 0, 0, text + 2 * TR_CHUNK_MAX,
			     TR_TEXT - 2 * TR_CHUNK_MAX));

	expect_frame("52 0a 00 30 02 03 00 01 00 c9");
	expect_frame("52 0a 00 31 02 03 00 02 00 c9");
	expect_frame("52 0a 00 32 02 03 00 03 00 c9");
	zassert_equal(tr()->len, TR_TEXT);
	zassert_mem_equal(tr()->data, text, TR_TEXT);
	feat_expect_guard_intact();
}

ZTEST(features, test_0x52_text_of_0x201_bytes_is_refused)
{
	static uint8_t text[TR_TEXT + 1];
	uint8_t pkt[0x100];

	memset(text, 'z', sizeof(text));
	tr_running(1);

	g1_send(pkt, tr_text(pkt, 0x30, 3, 1, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x31, 3, 2, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x32, 3, 3, 0, 0, 0, text, TR_TEXT + 1 - 2 * TR_CHUNK_MAX));

	expect_frame("52 0a 00 30 02 03 00 01 00 c9");
	expect_frame("52 0a 00 31 02 03 00 02 00 c9");
	expect_frame("52 0a 00 32 02 03 00 03 00 ca");
	zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_middle_chunk_reaching_0x200_is_accepted)
{
	static uint8_t text[TR_TEXT];
	uint8_t pkt[0x100];

	memset(text, 'm', sizeof(text));
	tr_running(1);

	g1_send(pkt, tr_text(pkt, 0x40, 4, 1, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x41, 4, 2, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x42, 4, 3, 0, 0, 0, text, TR_TEXT - 2 * TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x43, 4, 4, 0, 0, 0, text, 0));

	expect_frame("52 0a 00 40 02 04 00 01 00 c9");
	expect_frame("52 0a 00 41 02 04 00 02 00 c9");
	expect_frame("52 0a 00 42 02 04 00 03 00 c9");
	expect_frame("52 0a 00 43 02 04 00 04 00 c9");
	zassert_equal(tr()->len, TR_TEXT);
}

ZTEST(features, test_0x52_text_middle_chunk_beyond_0x200_is_refused)
{
	static uint8_t text[TR_TEXT + 1];
	uint8_t pkt[0x100];

	memset(text, 'm', sizeof(text));
	tr_running(1);

	g1_send(pkt, tr_text(pkt, 0x40, 4, 1, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x41, 4, 2, 0, 0, 0, text, TR_CHUNK_MAX));
	g1_send(pkt, tr_text(pkt, 0x42, 4, 3, 0, 0, 0, text, TR_TEXT + 1 - 2 * TR_CHUNK_MAX));

	expect_frame("52 0a 00 40 02 04 00 01 00 c9");
	expect_frame("52 0a 00 41 02 04 00 02 00 c9");
	expect_frame("52 0a 00 42 02 04 00 03 00 ca");
	zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_last_chunk_out_of_order_replies_ca)
{
	tr_running(1);
	tr_send_str(0x50, 3, 1, "one ");
	tr_send_str(0x51, 3, 3, "three");

	expect_frame("52 0a 00 50 02 03 00 01 00 c9");
	expect_frame("52 0a 00 51 02 03 00 03 00 ca");
	zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_middle_chunk_out_of_order_replies_ca)
{
	tr_running(1);
	tr_send_str(0x52, 4, 1, "one ");
	tr_send_str(0x53, 4, 3, "three ");

	expect_frame("52 0a 00 52 02 04 00 01 00 c9");
	expect_frame("52 0a 00 53 02 04 00 03 00 ca");
	zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_last_chunk_of_another_total_replies_ca)
{
	tr_running(1);
	tr_send_str(0x54, 3, 1, "one ");
	tr_send_str(0x55, 2, 2, "two");

	expect_frame("52 0a 00 54 02 03 00 01 00 c9");
	expect_frame("52 0a 00 55 02 02 00 02 00 ca");
	zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_chunk_index_beyond_total_is_ignored)
{
	tr_running(1);
	tr_send_str(0x56, 2, 1, "abc");
	tr_send_str(0x57, 2, 3, "zzz");
	tr_send_str(0x58, 2, 2, "def");

	feat_expect_field((const char *)tr()->data, TR_TEXT, "abcdef");
	zassert_equal(tr()->len, 6);
}

/* DEFECT: an index above the total keeps the initial C9. */
ZTEST_EXPECT_FAIL(features, test_0x52_text_chunk_index_beyond_total_replies_ca);
ZTEST(features, test_0x52_text_chunk_index_beyond_total_replies_ca)
{
	tr_running(1);
	tr_send_str(0x56, 2, 1, "abc");
	tr_send_str(0x57, 2, 3, "zzz");

	expect_frame("52 0a 00 56 02 02 00 01 00 c9");
	expect_frame("52 0a 00 57 02 02 00 03 00 ca");
}

/* DEFECT: the total-1 path clears multi_packet_buf but not multi_packet_len / _cnt / _total. */
ZTEST_EXPECT_FAIL(features, test_0x52_single_text_inside_a_chunked_text_does_not_corrupt_it);
ZTEST(features, test_0x52_single_text_inside_a_chunked_text_does_not_corrupt_it)
{
	tr_running(1);
	tr_send_str(0x60, 2, 1, "abc");
	tr_send_str(0x61, 1, 1, "x");
	tr_send_str(0x62, 2, 2, "def");

	zassert_equal(strnlen((const char *)tr()->data, TR_TEXT), tr()->len,
		      "text has a NUL inside its length %u", tr()->len);
}

/* DEFECT: copies data_len - 12 bytes, which underflows. */
ZTEST(features, test_0x52_text_chunk_shorter_than_its_header_is_refused)
{
	ztest_test_skip();

	tr_running(1);
	g1_send_hex("52 0b 00 18 02 02 00 01 00 00 00");

	expect_frame("52 0a 00 18 02 02 00 01 00 ca");
}

ZTEST(features, test_0x52_resets_the_screen_timeout)
{
	tr_running(1);

	g1_send_hex("52 06 00 1a 01 03");
	tr_send_str(0x1b, 1, 1, "abc");
	g1_send_hex("52 06 00 1c 03 00");

	zassert_equal(transcribe_info_timeout_reset_fake.call_count, 3);
}

ZTEST(features, test_0x52_unknown_sub_replies_ca)
{
	g1_send_hex("52 06 00 1d 00 01");
	g1_send_hex("52 06 00 1e 04 01");

	expect_frame("52 06 00 1d 00 ca");
	expect_frame("52 06 00 1e 04 ca");
	expect_no_frame();
	zassert_false(tr()->running);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
}

ZTEST(features, test_0x52_length_field_mismatch_replies_ca)
{
	g1_send_hex("52 07 00 1f 01 01");

	expect_frame("52 06 00 1f 01 ca");
	expect_no_frame();
	zassert_false(tr()->running);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
	zassert_equal(transcribe_info_timeout_reset_fake.call_count, 0);
}

ZTEST(features, test_0x53_heartbeat_resets_the_screen_timeout)
{
	tr_running(1);

	g1_send_hex("53 06 00 21 03 01");

	zassert_equal(transcribe_info_timeout_reset_fake.call_count, 1);
}

ZTEST(features, test_0x53_heartbeat_changes_no_transcribe_state)
{
	static uint8_t before[TR_BLOCK];

	tr_running(1);
	tr_send_str(0x22, 1, 1, "kept");
	memcpy(before, tr(), TR_BLOCK);

	g1_send_hex("53 06 00 23 03 01");

	zassert_mem_equal(tr(), before, TR_BLOCK, "transcribe_buf changed");
	zassert_equal(dmic_stream_stop_fake.call_count, 0);
	zassert_equal(dmic_stream_pause_fake.call_count, 0);
}

ZTEST(features, test_0x53_other_sub_replies_ca)
{
	tr_running(1);

	g1_send_hex("53 06 00 24 01 01");
	g1_send_hex("53 06 00 25 04 01");

	expect_frame("53 06 00 24 01 ca");
	expect_frame("53 06 00 25 04 ca");
	expect_no_frame();
	zassert_true(tr()->running);
}

ZTEST(features, test_0x53_length_field_mismatch_replies_ca)
{
	tr_running(1);

	g1_send_hex("53 07 00 26 03 01");

	expect_frame("53 06 00 26 03 ca");
	expect_no_frame();
	zassert_equal(transcribe_info_timeout_reset_fake.call_count, 0);
}
