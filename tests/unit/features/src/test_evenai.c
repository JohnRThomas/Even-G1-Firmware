/* Even AI: 0x54 answer text, 0x55 heartbeat, 0x56 exit. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "ble/ble_proto.h"
#include "features.h"

#define AI_HDR  12    /* sub 3 header */
#define AI_TEXT 0x200 /* sizeof(evenai_buf.data) */
#define AI_CHUNK_MAX (0x100 - AI_HDR) /* largest packet the link delivers */

static struct evenai_buf *ai(void)
{
	return g1_state()->evenai_buf;
}

static size_t ai_chunk(uint8_t *pkt, uint8_t seq, uint16_t total, uint16_t idx, uint8_t status,
		       uint8_t line, uint8_t pct, const uint8_t *text, size_t n)
{
	pkt[0] = 0x54;
	pkt[3] = seq;
	pkt[4] = 0x03;
	pkt[5] = (uint8_t)total;
	pkt[6] = (uint8_t)(total >> 8);
	pkt[7] = (uint8_t)idx;
	pkt[8] = (uint8_t)(idx >> 8);
	pkt[9] = status;
	pkt[10] = line;
	pkt[11] = pct;
	memcpy(&pkt[AI_HDR], text, n);
	return feat_set_len16(pkt, AI_HDR + n);
}

ZTEST(features, test_0x54_answer_text_is_stored_in_evenai_buf)
{
	g1_send_vector("0x54_evenai_text");

	zassert_equal(ai()->sub, 3);
	zassert_equal(ai()->action_status, 0);
	zassert_equal(ai()->scroll_line, 0);
	zassert_equal(ai()->scroll_pct, 100);
	zassert_equal(ai()->len, 31);
	feat_expect_field((const char *)ai()->data, AI_TEXT, "Paris is the capital of France.");
	expect_frame("54 0a 00 00 03 01 00 01 00 c9");
	expect_no_frame();
}

ZTEST(features, test_0x54_answer_replaces_the_previous_text)
{
	uint8_t pkt[64];

	g1_send_vector("0x54_evenai_text");
	g1_send(pkt, ai_chunk(pkt, 0x01, 1, 1, 0, 0, 50, (const uint8_t *)"Yes.", 4));

	feat_expect_field((const char *)ai()->data, AI_TEXT, "Yes.");
	zassert_equal(ai()->len, 4);
	zassert_equal(ai()->scroll_pct, 50);
}

ZTEST(features, test_0x54_answer_in_two_chunks_is_assembled)
{
	uint8_t pkt[64];

	g1_send(pkt, ai_chunk(pkt, 0x10, 2, 1, 0x01, 0x02, 10, (const uint8_t *)"Hello ", 6));
	expect_frame("54 0a 00 10 03 02 00 01 00 c9");
	zassert_equal(ai()->len, 0, "text shown before the last chunk");

	g1_send(pkt, ai_chunk(pkt, 0x11, 2, 2, 0x00, 0x03, 90, (const uint8_t *)"world", 5));
	expect_frame("54 0a 00 11 03 02 00 02 00 c9");

	feat_expect_field((const char *)ai()->data, AI_TEXT, "Hello world");
	zassert_equal(ai()->len, 11);
	zassert_equal(ai()->scroll_line, 3);
	zassert_equal(ai()->scroll_pct, 90);
}

/* 244 + 244 + 24 = 0x200 */
ZTEST(features, test_0x54_answer_of_0x200_bytes_is_accepted)
{
	static uint8_t text[AI_TEXT];
	uint8_t pkt[256];

	for (size_t i = 0; i < sizeof(text); i++) {
		text[i] = 'a' + i % 26;
	}
	g1_send(pkt, ai_chunk(pkt, 0x20, 3, 1, 0, 0, 0, text, AI_CHUNK_MAX));
	g1_send(pkt, ai_chunk(pkt, 0x21, 3, 2, 0, 0, 0, text + AI_CHUNK_MAX, AI_CHUNK_MAX));
	g1_send(pkt, ai_chunk(pkt, 0x22, 3, 3, 0, 0, 0, text + 2 * AI_CHUNK_MAX,
			      AI_TEXT - 2 * AI_CHUNK_MAX));

	expect_frame("54 0a 00 20 03 03 00 01 00 c9");
	expect_frame("54 0a 00 21 03 03 00 02 00 c9");
	expect_frame("54 0a 00 22 03 03 00 03 00 c9");
	zassert_equal(ai()->len, AI_TEXT);
	zassert_mem_equal(ai()->data, text, AI_TEXT);
}

ZTEST(features, test_0x54_answer_of_0x201_bytes_is_refused)
{
	static uint8_t text[AI_TEXT + 1];
	uint8_t pkt[256];

	memset(text, 'z', sizeof(text));
	g1_send(pkt, ai_chunk(pkt, 0x20, 3, 1, 0, 0, 0, text, AI_CHUNK_MAX));
	g1_send(pkt, ai_chunk(pkt, 0x21, 3, 2, 0, 0, 0, text, AI_CHUNK_MAX));
	g1_send(pkt, ai_chunk(pkt, 0x22, 3, 3, 0, 0, 0, text, AI_TEXT + 1 - 2 * AI_CHUNK_MAX));

	expect_frame("54 0a 00 20 03 03 00 01 00 c9");
	expect_frame("54 0a 00 21 03 03 00 02 00 c9");
	expect_frame("54 0a 00 22 03 03 00 03 00 ca");
	zassert_equal(ai()->len, 0);
}

ZTEST(features, test_0x54_length_field_mismatch_replies_ca)
{
	uint8_t pkt[64];
	size_t n = feat_vector("0x54_evenai_text", pkt, sizeof(pkt));

	pkt[1]++;
	g1_send(pkt, n);

	expect_frame("54 06 00 00 03 ca");
	zassert_equal(ai()->len, 0);
}

ZTEST(features, test_0x55_heartbeat_while_active_replies_c9)
{
	ai()->state = 1;
	g1_send_vector("0x55_evenai_heartbeat");

	expect_frame("55 07 00 01 05 c9 00");
	expect_no_frame();
	zassert_equal(even_ai_v2_info_timeout_reset_fake.call_count, 1);
}

ZTEST(features, test_0x55_heartbeat_while_inactive_replies_ca)
{
	g1_send_vector("0x55_evenai_heartbeat");

	expect_frame("55 07 00 01 05 ca 00");
}

ZTEST(features, test_0x56_exit_stops_the_mic_and_sets_exit_pending)
{
	g1_send_vector("0x56_evenai_exit");

	expect_frame("56 07 00 02 08 00 00");
	zassert_equal(g1_state()->dashboard_ts->exit_pending, 1);
	zassert_equal(dmic_stream_stop_fake.call_count, 1);
}

ZTEST(features, test_0x56_exit_with_flag_01_keeps_running)
{
	g1_send_hex("56 06 00 03 08 01");

	expect_frame("56 07 00 03 08 00 01");
	zassert_equal(g1_state()->dashboard_ts->exit_pending, 0);
	zassert_equal(dmic_stream_stop_fake.call_count, 0);
}
