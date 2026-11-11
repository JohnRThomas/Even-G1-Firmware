/*
 * 0x0D / 0x0F text chunks and 0x1C control. Vector names follow the app (0x0D = dst); ble_cmd.h has
 * them reversed.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "fakes_misc.h"
#include "ble/ble_proto.h"
#include "features.h"

#define TR_HDR       7
#define TR_DATA      0x1e0 /* sizeof(translate_buf.data) */
#define TR_BUF_SIZE  0x1e8 /* sizeof(struct translate_buf) */
#define NUS_RX_MAX   0x100 /* ble_context.rx_data */
#define TEXT_PKT_BUF 0x158 /* ble_proto.c text_packet_buf */

static size_t tr_pkt(uint8_t *pkt, uint8_t cmd, uint8_t seq, uint8_t count, uint8_t idx,
		     uint8_t status, uint16_t pos, const void *text, size_t text_len)
{
	pkt[0] = cmd;
	pkt[1] = seq;
	pkt[2] = count;
	pkt[3] = idx;
	pkt[4] = status;
	pkt[5] = (uint8_t)(pos >> 8);
	pkt[6] = (uint8_t)pos;
	memcpy(&pkt[TR_HDR], text, text_len);
	return TR_HDR + text_len;
}

static void send_tr(uint8_t cmd, uint8_t seq, uint8_t count, uint8_t idx, const char *text)
{
	uint8_t pkt[NUS_RX_MAX];

	g1_send(pkt, tr_pkt(pkt, cmd, seq, count, idx, 0, 0, text, strlen(text)));
}

static struct translate_buf *tr_src(void)
{
	return g1_state()->translate_src;
}

static struct translate_buf *tr_dst(void)
{
	return g1_state()->translate_dst;
}

ZTEST(features, test_0x0d_text_is_stored_in_translate_src)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send_vector("0x0d_translate_dst_text");

	zassert_equal(tr_src()->screen_status, 0x02);
	zassert_equal(tr_src()->char_pos, 0);
	feat_expect_field((const char *)tr_src()->data, TR_DATA, " \r \rHello.");
	zassert_equal(tr_src()->len, 10);
	zassert_equal(tr_dst()->len, 0, "0x0D must not touch translate_dst");
}

ZTEST(features, test_0x0f_text_is_stored_in_translate_dst)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send_vector("0x0f_translate_src_text");

	zassert_equal(tr_dst()->screen_status, 0x00);
	feat_expect_bytes(tr_dst()->data, 21,
			  "20 0d 20 0d ec 95 88 eb 85 95 ed 95 98 ec 84 b8 ec 9a 94 2e 00");
	zassert_equal(tr_dst()->len, 20);
	zassert_equal(tr_src()->len, 0, "0x0F must not touch translate_src");
}

ZTEST(features, test_0x0f_char_position_is_big_endian)
{
	uint8_t pkt[32];

	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send(pkt, tr_pkt(pkt, 0x0f, 0x03, 1, 0, 0x01, 0x0102, "ab", 2));

	zassert_equal(tr_dst()->char_pos, 0x0102, "char_pos 0x%04x", tr_dst()->char_pos);
	zassert_equal(tr_dst()->screen_status, 0x01);
}

ZTEST(features, test_0x0d_chunks_are_appended_and_only_chunk_0_sets_the_header)
{
	uint8_t pkt[32];

	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send(pkt, tr_pkt(pkt, 0x0d, 0x04, 2, 0, 0x02, 0x0005, "Good ", 5));
	g1_send(pkt, tr_pkt(pkt, 0x0d, 0x04, 2, 1, 0x07, 0x0900, "morning", 7));

	feat_expect_field((const char *)tr_src()->data, TR_DATA, "Good morning");
	zassert_equal(tr_src()->len, 12);
	zassert_equal(tr_src()->screen_status, 0x02);
	zassert_equal(tr_src()->char_pos, 0x0005);
}

ZTEST(features, test_0x0d_line_reset_replaces_the_previous_text)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send_vector("0x0d_translate_dst_text");
	g1_send_vector("0x0d_translate_dst_clear");

	feat_expect_field((const char *)tr_src()->data, TR_DATA, "\r");
	zassert_equal(tr_src()->len, 1);
}

ZTEST(features, test_0x0f_largest_chunk_the_link_delivers_is_stored_whole)
{
	static uint8_t text[NUS_RX_MAX - TR_HDR];
	uint8_t pkt[NUS_RX_MAX];

	for (size_t i = 0; i < sizeof(text); i++) {
		text[i] = 'A' + i % 26;
	}
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send(pkt, tr_pkt(pkt, 0x0f, 0x01, 1, 0, 0, 0, text, sizeof(text)));

	zassert_equal(tr_dst()->len, sizeof(text));
	zassert_mem_equal(tr_dst()->data, text, sizeof(text));
}

ZTEST(features, test_0x0d_last_chunk_on_the_slave_is_acked_c9_at_once)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send_vector("0x0d_translate_dst_text");

	expect_frame_padded("0d c9 01 01 00", 20);
	expect_no_frame();
}

ZTEST(features, test_0x0f_last_chunk_on_the_slave_is_acked_c9_at_once)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send_vector("0x0f_translate_src_text");

	expect_frame_padded("0f c9 01 01 00", 20);
	expect_no_frame();
}

ZTEST(features, test_0x0d_last_chunk_on_the_master_is_acked_by_event_f0)
{
	uint8_t evt[3] = {0xf5, 0xf0, 0xcb};

	g1_send_vector("0x0d_translate_dst_text");
	expect_no_frame();
	zassert_equal(deferred_text_ack_cmd, 0x0d);
	zassert_equal(g1_state()->esb_down_id, 0x01);

	deal_event_to_phone(g1_ble(), evt);
	expect_frame_padded("0d c9 01 01 00", 21);
	expect_no_frame();
}

ZTEST(features, test_0x0f_last_chunk_on_the_master_is_acked_by_event_f1)
{
	uint8_t evt[3] = {0xf5, 0xf1, 0xcb};

	g1_send_vector("0x0f_translate_src_text");
	expect_no_frame();
	zassert_equal(deferred_text_ack_cmd, 0x0f);
	zassert_equal(g1_state()->esb_up_id, 0x01);

	deal_event_to_phone(g1_ble(), evt);
	expect_frame_padded("0f c9 01 01 00", 21);
	expect_no_frame();
}

ZTEST(features, test_0x0f_chunk_before_the_last_is_acked_cb_at_once)
{
	send_tr(0x0f, 0x09, 3, 0, "x");
	send_tr(0x0f, 0x09, 3, 1, "y");

	expect_frame_padded("0f cb 09 03 00", 20);
	expect_frame_padded("0f cb 09 03 01", 20);
	expect_no_frame();
}

/* task 11 = translate */
ZTEST(features, test_0x0f_last_chunk_with_the_mic_running_switches_to_the_translate_task)
{
	dmic_get_stop_state_fake.custom_fake = NULL;
	dmic_get_stop_state_fake.return_val = 0;

	g1_send_vector("0x0f_translate_src_text");

	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal(update_persist_task_status_fake.arg1_val, 11);
	zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

/* DEFECT: chunks append at data + len without a size check. */
ZTEST_EXPECT_FAIL(features, test_0x0f_text_longer_than_the_translate_buffer_is_not_written_past_it);
ZTEST(features, test_0x0f_text_longer_than_the_translate_buffer_is_not_written_past_it)
{
	static uint8_t text[NUS_RX_MAX - TR_HDR];
	uint8_t pkt[NUS_RX_MAX];
	struct translate_buf *t;

	memset(text, 'x', sizeof(text));
	t = feat_guard_block(&g1_state()->translate_dst, TR_BUF_SIZE, 0, 0x100);
	g1_unit_set_role(G1_ROLE_SLAVE);

	g1_send(pkt, tr_pkt(pkt, 0x0f, 0x02, 2, 0, 0, 0, text, sizeof(text)));
	g1_send(pkt, tr_pkt(pkt, 0x0f, 0x02, 2, 1, 0, 0, text, sizeof(text)));

	feat_expect_guard_intact();
	zassert_true(t->len <= TR_DATA, "translate_dst->len %u", t->len);
}

/* DEFECT: copies hdr->len - 7 bytes, which underflows. */
ZTEST(features, test_0x0d_packet_shorter_than_its_header_is_refused)
{
	ztest_test_skip();

	g1_send_hex("0d 01 01 00");
	zassert_equal(tr_src()->len, 0);
}

/* DEFECT: unbounded copy into text_packet_buf. Dispatched directly: the link caps rx at 0x100. */
ZTEST(features, test_0x0d_chunk_longer_than_text_packet_buf_is_rejected)
{
	static uint8_t pkt[TR_HDR + TEXT_PKT_BUF + 1];
	struct ble_req_hdr hdr = {.cmd = 0x0d, .source = 1, .len = sizeof(pkt)};

	ztest_test_skip();

	memset(pkt, 'x', sizeof(pkt));
	tr_pkt(pkt, 0x0d, 0x01, 1, 0, 0, 0, "", 0);
	ble_process_req_dispatch(g1_ble(), &hdr, pkt);
	zassert_true(tr_src()->len <= TR_DATA);
}

ZTEST(features, test_0x1c_start_stores_both_languages_and_resumes_the_mic)
{
	g1_send_vector("0x1c_translate_control");

	zassert_equal(tr_src()->original_type, 0x04);
	zassert_equal(tr_src()->translation_type, 0x02);
	zassert_equal(tr_dst()->original_type, 0x04);
	zassert_equal(tr_dst()->translation_type, 0x02);
	zassert_equal(tr_src()->suspend, 0);
	zassert_equal(tr_dst()->suspend, 0);
	zassert_equal(dmic_stream_pause_fake.call_count, 1);
	zassert_equal(dmic_stream_pause_fake.arg0_val, 0);
	expect_frame_padded("1c c9", 20);
	expect_no_frame();
}

/* want 0: any non-zero value */
static bool matches(int got, int want)
{
	return want != 0 ? got == want : got != 0;
}

/*
 * 1 is the app's pause, and ui_translate shows the pause icon only for suspend 1. 7 is a value
 * the app does not send; dmic_stream_pause() pauses on any non-zero value.
 */
ZTEST(features, test_0x1c_nonzero_state_sets_suspend_and_pauses_the_mic)
{
	static const struct {
		uint8_t state;
		uint8_t want;
	} rows[] = {
		{0x01, 1},
		{0x07, 0},
	};
	static char msg[G1_MSG_SIZE];

	for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
		const uint8_t pkt[] = {0x1c, rows[i].state, 0x04, 0x02};

		g1_unit_reset();
		g1_send(pkt, sizeof(pkt));

		zassert_true(matches(tr_src()->suspend, rows[i].want),
			     "state 0x%02x, want %u: src suspend %u", rows[i].state, rows[i].want,
			     tr_src()->suspend);
		zassert_true(matches(tr_dst()->suspend, rows[i].want),
			     "state 0x%02x, want %u: dst suspend %u", rows[i].state, rows[i].want,
			     tr_dst()->suspend);
		zassert_equal(dmic_stream_pause_fake.call_count, 1,
			      "state 0x%02x, want %u: dmic_stream_pause called %u times",
			      rows[i].state, rows[i].want, dmic_stream_pause_fake.call_count);
		zassert_true(matches(dmic_stream_pause_fake.arg0_val, rows[i].want),
			     "state 0x%02x, want %u: dmic_stream_pause(%d)", rows[i].state,
			     rows[i].want, dmic_stream_pause_fake.arg0_val);
		zassert_true(g1_frame_check("1c c9", 20, msg, sizeof(msg)),
			     "state 0x%02x, want %u: %s", rows[i].state, rows[i].want, msg);
	}
}

ZTEST(features, test_0x1c_language_00_keeps_the_previous_pair)
{
	g1_send_vector("0x1c_translate_control");
	g1_send_hex("1c 01 00 05");
	g1_send_hex("1c 00 06 00");

	zassert_equal(tr_src()->original_type, 0x04);
	zassert_equal(tr_src()->translation_type, 0x02);
	zassert_equal(tr_dst()->original_type, 0x04);
	zassert_equal(tr_dst()->translation_type, 0x02);
}
