/*
 * 0x4E text set: 4E seq count index status pos_hi pos_lo page pages text. Status: style in the high
 * nibble, canvas state in the low. On the master the last chunk's ack waits for event 0xf2.
 * 0x0D / 0x0F translate text chunks (the same without page and pages) and 0x1C control. Vector
 * names follow the app (0x0D = dst); ble_cmd.h has them reversed.
 */
#include "features.h"

#define TRANSLATE_HDR      7     /* 0x0D / 0x0F header */
#define TEXT_HDR           9     /* 0x4E header */
#define DASH_DATA          0x1e0 /* sizeof(dash_buf->data) */
#define DASH_BUF_SIZE      0x1e9 /* sizeof(struct dash_data_buf) */
#define TRANSLATE_DATA     0x1e0 /* sizeof(translate_buf.data) */
#define TRANSLATE_BUF_SIZE 0x1e8 /* sizeof(struct translate_buf) */
#define NUS_RX_MAX         0x100 /* ble_context.rx_data */
#define TEXT_PKT_BUF       0x158 /* ble_proto.c text_packet_buf */

static struct dash_data_buf *dash(void)
{
  return g1_state()->dash_buf;
}

static struct translate_buf *translate_src(void)
{
  return g1_state()->translate_src;
}

static struct translate_buf *translate_dst(void)
{
  return g1_state()->translate_dst;
}

static void translate_send(uint8_t cmd, uint8_t seq, uint8_t count, uint8_t index, uint8_t status,
         uint16_t pos, const void *text, size_t text_len)
{
  uint8_t pkt[NUS_RX_MAX];

  pkt[0] = cmd;
  pkt[1] = seq;
  pkt[2] = count;
  pkt[3] = index;
  pkt[4] = status;
  sys_put_be16(pos, &pkt[5]);
  memcpy(&pkt[TRANSLATE_HDR], text, text_len);
  g1_send(pkt, TRANSLATE_HDR + text_len);
}

/* 0x4E is the 0x0D / 0x0F layout with page and pages in front of the text */
static void text_send(uint8_t seq, uint8_t count, uint8_t index, uint8_t status, uint16_t pos,
          uint8_t page, uint8_t pages, const void *text, size_t text_len)
{
  uint8_t data[NUS_RX_MAX];

  data[0] = page;
  data[1] = pages;
  memcpy(&data[2], text, text_len);
  translate_send(0x4e, seq, count, index, status, pos, data, 2 + text_len);
}

static void text_send_str(uint8_t seq, uint8_t count, uint8_t index, uint8_t status,
        const char *text)
{
  text_send(seq, count, index, status, 0, 1, 1, text, strlen(text));
}

ZTEST(features, test_0x4e_first_chunk_stores_status_char_position_scroll_and_page_count)
{
  text_send(0x05, 2, 0, 0x31, 0x0123, 0x2a, 0x05, "AB", 2);

  zassert_equal(dash()->screen_status, 0x31);
  zassert_equal(dash()->char_pos, 0x0123, "char_pos 0x%04x", dash()->char_pos);
  zassert_equal(dash()->scroll_pct, 0x2a);
  zassert_equal(dash()->page_count, 0x05);
  expect_bytes(dash()->data, 3, "41 42 00");
  zassert_equal(dash()->len, 2);
}

ZTEST(features, test_0x4e_chunks_are_appended_and_only_chunk_0_sets_the_header)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  text_send(0x07, 3, 0, 0x31, 0x0010, 1, 3, "Hello ", 6);
  text_send(0x07, 3, 1, 0x51, 0x0200, 2, 9, "from ", 5);
  text_send(0x07, 3, 2, 0x51, 0x0300, 3, 9, "G1", 2);

  feat_expect_field(dash()->data, DASH_DATA, "Hello from G1");
  zassert_equal(dash()->len, 13);
  zassert_equal(dash()->screen_status, 0x31);
  zassert_equal(dash()->char_pos, 0x0010);
  zassert_equal(dash()->scroll_pct, 1);
  zassert_equal(dash()->page_count, 3);
}

ZTEST(features, test_0x4e_chunk_0_starts_a_new_text)
{
  text_send_str(0x01, 3, 0, 0x31, "first ");
  text_send_str(0x01, 3, 1, 0x31, "part ");
  text_send_str(0x02, 2, 0, 0x71, "new");

  feat_expect_field(dash()->data, DASH_DATA, "new");
  zassert_equal(dash()->len, 3);
  zassert_equal(dash()->screen_status, 0x71);
}

ZTEST(features, test_0x4e_largest_chunk_the_link_delivers_is_stored_whole)
{
  static uint8_t text[NUS_RX_MAX - TEXT_HDR];

  for (size_t offset = 0; offset < sizeof(text); offset++) {
    text[offset] = 'a' + offset % 26;
  }
  g1_unit_set_role(G1_ROLE_SLAVE);
  text_send(0x10, 1, 0, 0x71, 0, 1, 1, text, sizeof(text));

  zassert_equal(dash()->len, sizeof(text));
  zassert_mem_equal(dash()->data, text, sizeof(text));
  zassert_equal(dash()->data[sizeof(text)], 0);
}

ZTEST(features, test_0x0d_text_is_stored_in_translate_src)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x0d_translate_dst_text");

  zassert_equal(translate_src()->screen_status, 0x02);
  zassert_equal(translate_src()->char_pos, 0);
  feat_expect_field(translate_src()->data, TRANSLATE_DATA, " \r \rHello.");
  zassert_equal(translate_src()->len, 10);
  zassert_equal(translate_dst()->len, 0, "0x0D must not touch translate_dst");
}

ZTEST(features, test_0x0f_text_is_stored_in_translate_dst)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x0f_translate_src_text");

  zassert_equal(translate_dst()->screen_status, 0x00);
  expect_bytes(translate_dst()->data, 21,
         "20 0d 20 0d ec 95 88 eb 85 95 ed 95 98 ec 84 b8 ec 9a 94 2e 00");
  zassert_equal(translate_dst()->len, 20);
  zassert_equal(translate_src()->len, 0, "0x0F must not touch translate_src");
}

ZTEST(features, test_0x0f_char_position_is_big_endian)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  translate_send(0x0f, 0x03, 1, 0, 0x01, 0x0102, "ab", 2);

  zassert_equal(translate_dst()->char_pos, 0x0102, "char_pos 0x%04x",
          translate_dst()->char_pos);
  zassert_equal(translate_dst()->screen_status, 0x01);
}

ZTEST(features, test_0x0d_chunks_are_appended_and_only_chunk_0_sets_the_header)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  translate_send(0x0d, 0x04, 2, 0, 0x02, 0x0005, "Good ", 5);
  translate_send(0x0d, 0x04, 2, 1, 0x07, 0x0900, "morning", 7);

  feat_expect_field(translate_src()->data, TRANSLATE_DATA, "Good morning");
  zassert_equal(translate_src()->len, 12);
  zassert_equal(translate_src()->screen_status, 0x02);
  zassert_equal(translate_src()->char_pos, 0x0005);
}

ZTEST(features, test_0x0d_line_reset_replaces_the_previous_text)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x0d_translate_dst_text");
  g1_send_vector("0x0d_translate_dst_clear");

  feat_expect_field(translate_src()->data, TRANSLATE_DATA, "\r");
  zassert_equal(translate_src()->len, 1);
}

ZTEST(features, test_0x0f_largest_chunk_the_link_delivers_is_stored_whole)
{
  static uint8_t text[NUS_RX_MAX - TRANSLATE_HDR];

  for (size_t offset = 0; offset < sizeof(text); offset++) {
    text[offset] = 'A' + offset % 26;
  }
  g1_unit_set_role(G1_ROLE_SLAVE);
  translate_send(0x0f, 0x01, 1, 0, 0, 0, text, sizeof(text));

  zassert_equal(translate_dst()->len, sizeof(text));
  zassert_mem_equal(translate_dst()->data, text, sizeof(text));
}

/* on the slave the last chunk is acked c9 at once */
#define SLAVE_ACK_TEST(name, vector, reply)                                                        \
  ZTEST(features, test_##name)                                                               \
  {                                                                                          \
    g1_unit_set_role(G1_ROLE_SLAVE);                                                   \
    g1_send_vector(vector);                                                            \
    expect_only_frame_padded(reply, 20);                                               \
  }
SLAVE_ACK_TEST(0x4e_last_chunk_on_the_slave_is_acked_c9_at_once, "0x4e_text_style7",
         "4e c9 00 01 00")
SLAVE_ACK_TEST(0x0d_last_chunk_on_the_slave_is_acked_c9_at_once, "0x0d_translate_dst_text",
         "0d c9 01 01 00")
SLAVE_ACK_TEST(0x0f_last_chunk_on_the_slave_is_acked_c9_at_once, "0x0f_translate_src_text",
         "0f c9 01 01 00")

ZTEST(features, test_0x4e_last_chunk_on_the_master_is_acked_by_event_f2)
{
  uint8_t evt[3] = {0xf5, 0xf2, 0xcb}; /* fed by ble_work_thread() for event 0xf2 */

  g1_send_vector_with("0x4e_text_style7", 1, 0x37); /* seq */

  expect_no_frame();
  zassert_equal(deferred_text_ack_cmd, 0x4e);
  zassert_equal(deferred_text_ack_total, 1);
  zassert_equal(deferred_text_ack_idx, 0);
  zassert_equal(g1_state()->esb_up_id, 0x37);

  deal_event_to_phone(g1_ble(), evt);

  expect_only_frame_padded("4e c9 37 01 00", 21);
  zassert_equal(deferred_text_ack_total, 0);
  zassert_equal(deferred_text_ack_idx, 0);
}

ZTEST(features, test_0x4e_deferred_ack_names_the_last_chunk_of_a_multi_chunk_text)
{
  uint8_t evt[3] = {0xf5, 0xf2, 0xcb};

  text_send_str(0x21, 3, 0, 0x71, "a");
  text_send_str(0x21, 3, 1, 0x71, "b");
  text_send_str(0x21, 3, 2, 0x71, "c");
  expect_frame_padded("4e cb 21 03 00", 20);
  expect_only_frame_padded("4e cb 21 03 01", 20);

  deal_event_to_phone(g1_ble(), evt);
  expect_frame_padded("4e c9 21 03 02", 21);
}

ZTEST(features, test_0x0d_last_chunk_on_the_master_is_acked_by_event_f0)
{
  uint8_t evt[3] = {0xf5, 0xf0, 0xcb};

  g1_send_vector("0x0d_translate_dst_text");
  expect_no_frame();
  zassert_equal(deferred_text_ack_cmd, 0x0d);
  zassert_equal(g1_state()->esb_down_id, 0x01);

  deal_event_to_phone(g1_ble(), evt);
  expect_only_frame_padded("0d c9 01 01 00", 21);
}

ZTEST(features, test_0x0f_last_chunk_on_the_master_is_acked_by_event_f1)
{
  uint8_t evt[3] = {0xf5, 0xf1, 0xcb};

  g1_send_vector("0x0f_translate_src_text");
  expect_no_frame();
  zassert_equal(deferred_text_ack_cmd, 0x0f);
  zassert_equal(g1_state()->esb_up_id, 0x01);

  deal_event_to_phone(g1_ble(), evt);
  expect_only_frame_padded("0f c9 01 01 00", 21);
}

ZTEST(features, test_0x0f_chunk_before_the_last_is_acked_cb_at_once)
{
  translate_send(0x0f, 0x09, 3, 0, 0, 0, "x", 1);
  translate_send(0x0f, 0x09, 3, 1, 0, 0, "y", 1);

  expect_frame_padded("0f cb 09 03 00", 20);
  expect_only_frame_padded("0f cb 09 03 01", 20);
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

/* task 11 = translate */
ZTEST(features, test_0x0f_last_chunk_with_the_mic_running_switches_to_the_translate_task)
{
  G1_FAKE_RETURN(dmic_get_stop_state, 0);

  g1_send_vector("0x0f_translate_src_text");

  zassert_equal(update_persist_task_status_fake.call_count, 1);
  zassert_equal(update_persist_task_status_fake.arg1_val, 11);
  zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

ZTEST(features, test_0x4e_style_7_text_only_sets_type_15_without_stay_time)
{
  g1_state()->stay_time_left_s = 99;
  g1_send_vector("0x4e_text_style7");

  zassert_equal(dash()->type, 15, "type %d", dash()->type);
  zassert_equal(g1_state()->stay_time_left_s, 0);
  feat_expect_field(dash()->data, DASH_DATA,
        "Hello from the G1 test vectors. One page, style 7.");
  zassert_equal(dash()->len, 50);
  zassert_equal(dash()->screen_status, 0x71);
}

ZTEST(features, test_0x4e_style_6_network_error_stops_the_mic_and_sets_type_10)
{
  text_send_str(0x02, 1, 0, 0x61, "No network");

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
  text_send_str(0x02, 1, 0, 0x30, "thinking");

  zassert_equal(dash()->type, 7, "type %d", dash()->type);
  zassert_equal(g1_state()->stay_time_left_s, 16);
}

ZTEST(features, test_0x4e_style_4_complete_sets_type_8_for_8_s)
{
  text_send_str(0x02, 1, 0, 0x40, "done");

  zassert_equal(dash()->type, 8, "type %d", dash()->type);
  zassert_equal(g1_state()->stay_time_left_s, 8);
}

ZTEST(features, test_0x4e_text_longer_than_dash_buf_is_not_written_past_it)
{
  static uint8_t text[NUS_RX_MAX - TEXT_HDR];

  memset(text, 'x', sizeof(text));
  feat_guard_block(&g1_state()->dash_buf, DASH_BUF_SIZE, 0, 0x100);
  g1_unit_set_role(G1_ROLE_SLAVE);

  text_send(0x30, 2, 0, 0x71, 0, 1, 1, text, sizeof(text));
  text_send(0x30, 2, 1, 0x71, 0, 1, 1, text, sizeof(text));

  feat_expect_guard_intact();
  zassert_true(dash()->len <= DASH_DATA, "dash_buf->len %u", dash()->len);
}

ZTEST(features, test_0x4e_packet_shorter_than_its_header_is_refused)
{
  g1_send_hex("4e 01 01 00 71");
  zassert_equal(dash()->len, 0);
}

/* Dispatched directly: the link caps rx at 0x100. */
ZTEST(features, test_0x4e_chunk_longer_than_multi_packet_buf_is_rejected)
{
  static uint8_t pkt[TEXT_HDR + 0x205];
  struct ble_req_hdr hdr = {.cmd = 0x4e, .source = 1, .len = sizeof(pkt)};

  memset(pkt, 'x', sizeof(pkt));
  g1_hex_parse("4e 01 01 00 71 00 00 01 01", pkt, sizeof(pkt));
  ble_process_req_dispatch(g1_ble(), &hdr, pkt);
  zassert_true(dash()->len <= DASH_DATA);
}

ZTEST(features, test_0x0f_text_longer_than_the_translate_buffer_is_not_written_past_it)
{
  static uint8_t text[NUS_RX_MAX - TRANSLATE_HDR];

  memset(text, 'x', sizeof(text));
  feat_guard_block(&g1_state()->translate_dst, TRANSLATE_BUF_SIZE, 0, 0x100);
  g1_unit_set_role(G1_ROLE_SLAVE);

  translate_send(0x0f, 0x02, 2, 0, 0, 0, text, sizeof(text));
  translate_send(0x0f, 0x02, 2, 1, 0, 0, text, sizeof(text));

  feat_expect_guard_intact();
  zassert_true(translate_dst()->len <= TRANSLATE_DATA, "translate_dst->len %u",
         translate_dst()->len);
}

ZTEST(features, test_0x0d_packet_shorter_than_its_header_is_refused)
{
  g1_send_hex("0d 01 01 00");
  zassert_equal(translate_src()->len, 0);
}

/* Dispatched directly: the link caps rx at 0x100. */
ZTEST(features, test_0x0d_chunk_longer_than_text_packet_buf_is_rejected)
{
  static uint8_t pkt[TRANSLATE_HDR + TEXT_PKT_BUF + 1];
  struct ble_req_hdr hdr = {.cmd = 0x0d, .source = 1, .len = sizeof(pkt)};

  memset(pkt, 'x', sizeof(pkt));
  g1_hex_parse("0d 01 01 00 00 00 00", pkt, sizeof(pkt));
  ble_process_req_dispatch(g1_ble(), &hdr, pkt);
  zassert_true(translate_src()->len <= TRANSLATE_DATA);
}

ZTEST(features, test_0x1c_start_stores_both_languages_and_resumes_the_mic)
{
  g1_send_vector("0x1c_translate_control");

  zassert_equal(translate_src()->original_type, 0x04);
  zassert_equal(translate_src()->translation_type, 0x02);
  zassert_equal(translate_dst()->original_type, 0x04);
  zassert_equal(translate_dst()->translation_type, 0x02);
  zassert_equal(translate_src()->suspend, 0);
  zassert_equal(translate_dst()->suspend, 0);
  zassert_equal(dmic_stream_pause_fake.call_count, 1);
  zassert_equal(dmic_stream_pause_fake.arg0_val, 0);
  expect_only_frame_padded("1c c9", 20);
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

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    const uint8_t pkt[] = {0x1c, rows[row].state, 0x04, 0x02};

    g1_unit_reset();
    g1_send(pkt, sizeof(pkt));

    zassert_true(matches(translate_src()->suspend, rows[row].want),
           "state 0x%02x, want %u: src suspend %u", rows[row].state,
           rows[row].want, translate_src()->suspend);
    zassert_true(matches(translate_dst()->suspend, rows[row].want),
           "state 0x%02x, want %u: dst suspend %u", rows[row].state,
           rows[row].want, translate_dst()->suspend);
    zassert_equal(dmic_stream_pause_fake.call_count, 1,
            "state 0x%02x, want %u: dmic_stream_pause called %u times",
            rows[row].state, rows[row].want, dmic_stream_pause_fake.call_count);
    zassert_true(matches(dmic_stream_pause_fake.arg0_val, rows[row].want),
           "state 0x%02x, want %u: dmic_stream_pause(%d)", rows[row].state,
           rows[row].want, dmic_stream_pause_fake.arg0_val);
    expect_frame_msg("1c c9", 20, "state 0x%02x, want %u", rows[row].state,
         rows[row].want);
  }
}

ZTEST(features, test_0x1c_language_00_keeps_the_previous_pair)
{
  g1_send_vector("0x1c_translate_control");
  g1_send_hex("1c 01 00 05");
  g1_send_hex("1c 00 06 00");

  zassert_equal(translate_src()->original_type, 0x04);
  zassert_equal(translate_src()->translation_type, 0x02);
  zassert_equal(translate_dst()->original_type, 0x04);
  zassert_equal(translate_dst()->translation_type, 0x02);
}
