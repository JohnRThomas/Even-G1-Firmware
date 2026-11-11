/*
 * 0x4E text set: 4E seq count index status pos_hi pos_lo page pages text. Status: style in the high
 * nibble, canvas state in the low. On the master the last chunk's ack waits for event 0xf2.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "fakes_misc.h"
#include "ble/ble_proto.h"
#include "features.h"

#define TEXT_HDR      9
#define DASH_DATA     0x1e0 /* sizeof(dash_buf->data) */
#define DASH_BUF_SIZE 0x1e9 /* sizeof(struct dash_data_buf) */
#define NUS_RX_MAX    0x100 /* ble_context.rx_data */

static size_t text_pkt(uint8_t *pkt, uint8_t seq, uint8_t count, uint8_t idx, uint8_t status,
		       uint16_t pos, uint8_t page, uint8_t pages, const void *text, size_t text_len)
{
	pkt[0] = 0x4e;
	pkt[1] = seq;
	pkt[2] = count;
	pkt[3] = idx;
	pkt[4] = status;
	pkt[5] = (uint8_t)(pos >> 8);
	pkt[6] = (uint8_t)pos;
	pkt[7] = page;
	pkt[8] = pages;
	memcpy(&pkt[TEXT_HDR], text, text_len);
	return TEXT_HDR + text_len;
}

static void send_text(uint8_t seq, uint8_t count, uint8_t idx, uint8_t status, const char *text)
{
	uint8_t pkt[NUS_RX_MAX];

	g1_send(pkt, text_pkt(pkt, seq, count, idx, status, 0, 1, 1, text, strlen(text)));
}

static struct dash_data_buf *dash(void)
{
	return g1_state()->dash_buf;
}

ZTEST(features, test_0x4e_first_chunk_stores_status_char_position_scroll_and_page_count)
{
	uint8_t pkt[32];

	g1_send(pkt, text_pkt(pkt, 0x05, 2, 0, 0x31, 0x0123, 0x2a, 0x05, "AB", 2));

	zassert_equal(dash()->screen_status, 0x31);
	zassert_equal(dash()->char_pos, 0x0123, "char_pos 0x%04x", dash()->char_pos);
	zassert_equal(dash()->scroll_pct, 0x2a);
	zassert_equal(dash()->page_count, 0x05);
	feat_expect_bytes(dash()->data, 3, "41 42 00");
	zassert_equal(dash()->len, 2);
}

ZTEST(features, test_0x4e_chunks_are_appended_and_only_chunk_0_sets_the_header)
{
	uint8_t pkt[32];

	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send(pkt, text_pkt(pkt, 0x07, 3, 0, 0x31, 0x0010, 1, 3, "Hello ", 6));
	g1_send(pkt, text_pkt(pkt, 0x07, 3, 1, 0x51, 0x0200, 2, 9, "from ", 5));
	g1_send(pkt, text_pkt(pkt, 0x07, 3, 2, 0x51, 0x0300, 3, 9, "G1", 2));

	feat_expect_field((const char *)dash()->data, DASH_DATA, "Hello from G1");
	zassert_equal(dash()->len, 13);
	zassert_equal(dash()->screen_status, 0x31);
	zassert_equal(dash()->char_pos, 0x0010);
	zassert_equal(dash()->scroll_pct, 1);
	zassert_equal(dash()->page_count, 3);
}

ZTEST(features, test_0x4e_chunk_0_starts_a_new_text)
{
	send_text(0x01, 3, 0, 0x31, "first ");
	send_text(0x01, 3, 1, 0x31, "part ");
	send_text(0x02, 2, 0, 0x71, "new");

	feat_expect_field((const char *)dash()->data, DASH_DATA, "new");
	zassert_equal(dash()->len, 3);
	zassert_equal(dash()->screen_status, 0x71);
}

ZTEST(features, test_0x4e_largest_chunk_the_link_delivers_is_stored_whole)
{
	static uint8_t text[NUS_RX_MAX - TEXT_HDR];
	uint8_t pkt[NUS_RX_MAX];

	for (size_t i = 0; i < sizeof(text); i++) {
		text[i] = 'a' + i % 26;
	}
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send(pkt, text_pkt(pkt, 0x10, 1, 0, 0x71, 0, 1, 1, text, sizeof(text)));

	zassert_equal(dash()->len, sizeof(text));
	zassert_mem_equal(dash()->data, text, sizeof(text));
	zassert_equal(dash()->data[sizeof(text)], 0);
}

ZTEST(features, test_0x4e_last_chunk_on_the_slave_is_acked_c9_at_once)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	g1_send_vector("0x4e_text_style7");

	expect_frame_padded("4e c9 00 01 00", 20);
	expect_no_frame();
}

ZTEST(features, test_0x4e_last_chunk_on_the_master_is_acked_by_event_f2)
{
	uint8_t pkt[256];
	uint8_t evt[3] = {0xf5, 0xf2, 0xcb}; /* fed by ble_work_thread() for event 0xf2 */
	size_t n = feat_vector("0x4e_text_style7", pkt, sizeof(pkt));

	pkt[1] = 0x37;
	g1_send(pkt, n);

	expect_no_frame();
	zassert_equal(deferred_text_ack_cmd, 0x4e);
	zassert_equal(deferred_text_ack_total, 1);
	zassert_equal(deferred_text_ack_idx, 0);
	zassert_equal(g1_state()->esb_up_id, 0x37);

	deal_event_to_phone(g1_ble(), evt);

	expect_frame_padded("4e c9 37 01 00", 21);
	expect_no_frame();
	zassert_equal(deferred_text_ack_total, 0);
	zassert_equal(deferred_text_ack_idx, 0);
}

ZTEST(features, test_0x4e_deferred_ack_names_the_last_chunk_of_a_multi_chunk_text)
{
	uint8_t evt[3] = {0xf5, 0xf2, 0xcb};

	send_text(0x21, 3, 0, 0x71, "a");
	send_text(0x21, 3, 1, 0x71, "b");
	send_text(0x21, 3, 2, 0x71, "c");
	expect_frame_padded("4e cb 21 03 00", 20);
	expect_frame_padded("4e cb 21 03 01", 20);
	expect_no_frame();

	deal_event_to_phone(g1_ble(), evt);
	expect_frame_padded("4e c9 21 03 02", 21);
}

/* task 16 = text */
ZTEST(features, test_0x4e_last_chunk_switches_to_the_text_task)
{
	g1_send_vector("0x4e_text_style7");

	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal_ptr(update_persist_task_status_fake.arg0_val, g1_state());
	zassert_equal(update_persist_task_status_fake.arg1_val, 16);
	zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

ZTEST(features, test_0x4e_style_7_text_only_sets_type_15_without_stay_time)
{
	g1_state()->stay_time_left_s = 99;
	g1_send_vector("0x4e_text_style7");

	zassert_equal(dash()->type, 15, "type %d", dash()->type);
	zassert_equal(g1_state()->stay_time_left_s, 0);
	feat_expect_field((const char *)dash()->data, DASH_DATA,
			  "Hello from the G1 test vectors. One page, style 7.");
	zassert_equal(dash()->len, 50);
	zassert_equal(dash()->screen_status, 0x71);
}

ZTEST(features, test_0x4e_style_6_network_error_stops_the_mic_and_sets_type_10)
{
	send_text(0x02, 1, 0, 0x61, "No network");

	zassert_equal(dmic_stream_stop_fake.call_count, 1);
	zassert_equal(dash()->type, 10, "type %d", dash()->type);
}

ZTEST(features, test_0x4e_style_7_does_not_stop_the_mic)
{
	g1_send_vector("0x4e_text_style7");

	zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

ZTEST(features, test_0x4e_style_3_auto_scroll_sets_type_7_for_16_s)
{
	send_text(0x02, 1, 0, 0x30, "thinking");

	zassert_equal(dash()->type, 7, "type %d", dash()->type);
	zassert_equal(g1_state()->stay_time_left_s, 16);
}

ZTEST(features, test_0x4e_style_4_complete_sets_type_8_for_8_s)
{
	send_text(0x02, 1, 0, 0x40, "done");

	zassert_equal(dash()->type, 8, "type %d", dash()->type);
	zassert_equal(g1_state()->stay_time_left_s, 8);
}

/* DEFECT: chunks append at dash_buf->data + len without a size check. */
ZTEST_EXPECT_FAIL(features, test_0x4e_text_longer_than_dash_buf_is_not_written_past_it);
ZTEST(features, test_0x4e_text_longer_than_dash_buf_is_not_written_past_it)
{
	static uint8_t text[NUS_RX_MAX - TEXT_HDR];
	uint8_t pkt[NUS_RX_MAX];
	struct dash_data_buf *d;

	memset(text, 'x', sizeof(text));
	d = feat_guard_block(&g1_state()->dash_buf, DASH_BUF_SIZE, 0, 0x100);
	g1_unit_set_role(G1_ROLE_SLAVE);

	g1_send(pkt, text_pkt(pkt, 0x30, 2, 0, 0x71, 0, 1, 1, text, sizeof(text)));
	g1_send(pkt, text_pkt(pkt, 0x30, 2, 1, 0x71, 0, 1, 1, text, sizeof(text)));

	feat_expect_guard_intact();
	zassert_true(d->len <= DASH_DATA, "dash_buf->len %u", d->len);
}

/* DEFECT: copies hdr->len - 9 bytes, which underflows. */
ZTEST(features, test_0x4e_packet_shorter_than_its_header_is_refused)
{
	ztest_test_skip();

	g1_send_hex("4e 01 01 00 71");
	zassert_equal(dash()->len, 0);
}

/* DEFECT: unbounded copy into multi_packet_buf. Dispatched directly: the link caps rx at 0x100. */
ZTEST(features, test_0x4e_chunk_longer_than_multi_packet_buf_is_rejected)
{
	static uint8_t pkt[TEXT_HDR + 0x205];
	struct ble_req_hdr hdr = {.cmd = 0x4e, .source = 1, .len = sizeof(pkt)};

	ztest_test_skip();

	memset(pkt, 'x', sizeof(pkt));
	text_pkt(pkt, 0x01, 1, 0, 0x71, 0, 1, 1, "", 0);
	ble_process_req_dispatch(g1_ble(), &hdr, pkt);
	zassert_true(dash()->len <= DASH_DATA);
}
