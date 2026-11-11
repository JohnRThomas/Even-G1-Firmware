/*
 * 0x09 teleprompter (sub 1 set text, 2 position, 3 update, 5 exit), 0x24 suspend, 0x25 heartbeat.
 * On the master, replies are queued for the phone.
 */
#include "features.h"

#define TP_TEXT 0x200 /* sizeof(teleprompter_info.text) */

static struct teleprompter_info *tp(void)
{
  return g1_state()->teleprompter;
}

/* sends a chunk; b9..b11: countdown, mode, scroll (sub 1) or scroll, mark16 (sub 3) */
static void tp_send(uint8_t seq, uint8_t sub, uint16_t total, uint16_t index, uint8_t b9,
        uint8_t b10, uint8_t b11, const char *text, bool with_ts, uint64_t ts)
{
  uint8_t pkt[0x100];
  uint8_t payload[sizeof(pkt) - G1_CHUNK_HDR_LEN] = {b9, b10, b11};
  size_t len = strlen(text);

  memcpy(&payload[3], text, len);
  len += 3;
  if (with_ts) {
    sys_put_le64(ts, &payload[len]);
    len += 8;
  }
  g1_send(pkt, g1_put_chunk(pkt, 0x09, seq, sub, total, index, payload, len));
}

ZTEST(features, test_0x09_set_text_fills_every_teleprompter_info_field)
{
  g1_send_vector("0x09_teleprompter_init");

  zassert_equal(tp()->mode, 1);
  zassert_equal(tp()->ready, 1);
  zassert_equal(tp()->ts, 1762981198349LL, "ts %lld", tp()->ts);
  zassert_equal(tp()->action, 1);
  zassert_equal(tp()->mode_flag, 0);
  zassert_equal(tp()->countdown_s, 3);
  zassert_equal(tp()->scroll_pct, 0);
  zassert_equal(tp()->mark_pos, 0);
  zassert_equal(tp()->suspend, 0);
  zassert_equal(tp()->counter, 0);
  zassert_equal(tp()->len, 43);
  feat_expect_field(tp()->text, TP_TEXT, "Teleprompter test text. Line one.\nLine two.");
}

ZTEST(features, test_0x09_set_text_splits_mode_and_mode_flag)
{
  tp_send(0x10, 1, 1, 1, 5, 0x83, 0x20, "abc", true, 1000);

  zassert_equal(tp()->mode, 3);
  zassert_equal(tp()->mode_flag, 0x80);
  zassert_equal(tp()->countdown_s, 5);
  zassert_equal(tp()->scroll_pct, 0x20);
  zassert_equal(tp()->ts, 1000);
  feat_expect_field(tp()->text, TP_TEXT, "abc");
}

/* task 9 = teleprompter */
ZTEST(features, test_0x09_set_text_starts_the_teleprompter_task)
{
  g1_send_vector("0x09_teleprompter_init");

  zassert_equal(update_persist_task_status_fake.call_count, 1);
  zassert_equal(update_persist_task_status_fake.arg1_val, 9);
  zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

ZTEST(features, test_0x09_set_text_reply_is_queued_on_the_master)
{
  g1_send_vector("0x09_teleprompter_init");

  feat_expect_queued_count(1);
  expect_pkt(&g1_esb_response_log, 0, "09 3f 00 00 01 01 00 01 00 00");
  expect_no_frame();
}

ZTEST(features, test_0x09_set_text_reply_is_written_at_once_on_the_slave)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x09_teleprompter_init");

  expect_only_frame("09 3f 00 00 01 01 00 01 00 00");
  feat_expect_queued_count(0);
}

ZTEST(features, test_0x09_set_text_in_two_chunks_is_assembled)
{
  tp_send(0x20, 1, 2, 1, 4, 0x02, 0x10, "first half, ", false, 0);
  expect_frame("09 18 00 20 01 02 00 01 00 00");
  zassert_equal(tp()->ready, 0, "nothing shown before the last chunk");

  tp_send(0x21, 1, 2, 2, 0, 0, 0, "second half", true, 77777);

  feat_expect_field(tp()->text, TP_TEXT, "first half, second half");
  zassert_equal(tp()->len, 23);
  zassert_equal(tp()->ready, 1);
  zassert_equal(tp()->action, 1);
  zassert_equal(tp()->mode, 2);
  zassert_equal(tp()->countdown_s, 4);
  zassert_equal(tp()->scroll_pct, 0x10);
  zassert_equal(tp()->ts, 77777);
  expect_pkt(&g1_esb_response_log, 0, "09 1f 00 21 01 02 00 02 00 00");
}

ZTEST(features, test_0x09_set_text_chunk_out_of_order_replies_01)
{
  tp_send(0x20, 1, 3, 1, 4, 0x02, 0x10, "one ", false, 0);
  expect_frame("09 10 00 20 01 03 00 01 00 00");
  tp_send(0x21, 1, 3, 3, 0, 0, 0, "three", true, 5);

  expect_frame("09 19 00 21 01 03 00 03 00 01");
  zassert_equal(tp()->ready, 0);
  zassert_equal(tp()->len, 0);
}

ZTEST(features, test_0x09_length_field_mismatch_is_dropped)
{
  g1_send_vector_with("0x09_teleprompter_init", 1, 0x40); /* length 0x3f + 1 */

  expect_no_frame();
  feat_expect_queued_count(0);
  zassert_equal(tp()->ready, 0);
}

ZTEST(features, test_0x09_position_sets_scroll_mark_and_timestamp)
{
  g1_send_vector("0x09_teleprompter_init");
  g1_send_vector("0x09_teleprompter_position");

  zassert_equal(tp()->scroll_pct, 50);
  zassert_equal(tp()->mark_pos, 20);
  zassert_equal(tp()->action, 2);
  zassert_equal(tp()->ts, 1762981203349LL, "ts %lld", tp()->ts);
  zassert_equal(tp()->len, 43);
}

ZTEST(features, test_0x09_position_is_echoed)
{
  g1_send_vector("0x09_teleprompter_position");
  expect_pkt(&g1_esb_response_log, 0, "09 10 00 01 02 00");
  expect_no_frame();

  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x09_teleprompter_position");
  expect_frame("09 10 00 01 02 00");
}

ZTEST(features, test_0x09_update_text_replaces_the_text)
{
  g1_send_vector("0x09_teleprompter_init");
  tp_send(0x30, 3, 1, 1, 0x40, 0x05, 0x01, "new", true, 123456);

  feat_expect_field(tp()->text, TP_TEXT, "new");
  zassert_equal(tp()->len, 3);
  zassert_equal(tp()->action, 3);
  zassert_equal(tp()->ready, 1);
  zassert_equal(tp()->scroll_pct, 0x40);
  zassert_equal(tp()->mark_pos, 0x0105);
  zassert_equal(tp()->ts, 123456);
  expect_pkt(&g1_esb_response_log, 1, "09 17 00 30 03 01 00 01 00 00");
}

ZTEST(features, test_0x09_update_text_longer_than_0x200_is_not_written_past_the_block)
{
  static char text[0x100 - 12 + 1];

  memset(text, 'u', sizeof(text) - 1);
  feat_guard_block(&g1_state()->teleprompter, sizeof(struct teleprompter_info), 0, 0x40);
  g1_unit_set_role(G1_ROLE_SLAVE);

  tp_send(0x40, 3, 3, 1, 0, 0, 0, text, false, 0);
  tp_send(0x41, 3, 3, 2, 0, 0, 0, text, false, 0);
  text[30] = '\0';
  tp_send(0x42, 3, 3, 3, 0, 0, 0, text, true, 1);

  feat_expect_guard_intact();
  zassert_true(tp()->len <= TP_TEXT, "len %u", tp()->len);
}

ZTEST(features, test_0x09_set_text_longer_than_0x200_is_refused)
{
  static char text[0x100 - 12 + 1];

  memset(text, 's', sizeof(text) - 1);
  tp_send(0x50, 1, 3, 1, 3, 1, 0, text, false, 0);
  tp_send(0x51, 1, 3, 2, 0, 0, 0, text, false, 0);
  tp_send(0x52, 1, 3, 3, 0, 0, 0, text, false, 0);
  zassert_true(tp()->len <= TP_TEXT);
}

ZTEST(features, test_0x09_set_text_shorter_than_its_header_is_refused)
{
  g1_send_hex("09 09 00 00 01 01 00 01 00");
  zassert_equal(tp()->ready, 0);
}

ZTEST(features, test_0x09_exit_clears_the_teleprompter_and_echoes)
{
  g1_send_vector("0x09_teleprompter_init");
  g1_send_vector("0x09_teleprompter_exit");

  expect_fill(tp(), sizeof(struct teleprompter_info), 0, "teleprompter_info");
  expect_only_frame("09 06 00 02 05 01");
}

ZTEST(features, test_0x24_pause_sets_suspend_timestamp_and_counter)
{
  g1_send_vector("0x09_teleprompter_init");
  g1_send_vector("0x24_teleprompter_pause");

  zassert_equal(tp()->suspend, 1);
  zassert_equal(tp()->ts, 1762981204349LL, "ts %lld", tp()->ts);
  zassert_equal(tp()->counter, 6);
  expect_pkt(&g1_esb_response_log, 1, "24 12 00 03 06 01");
  expect_no_frame();
}

ZTEST(features, test_0x24_byte_00_resumes)
{
  g1_send_vector("0x24_teleprompter_pause");
  zassert_equal(tp()->suspend, 1);
  g1_send_vector_with("0x24_teleprompter_pause", 5, 0x00);
  zassert_equal(tp()->suspend, 0);
}

ZTEST(features, test_0x24_other_sub_is_ignored)
{
  g1_send_vector_with("0x24_teleprompter_pause", 4, 0x07);

  zassert_equal(tp()->suspend, 0);
  zassert_equal(tp()->counter, 0);
  feat_expect_queued_count(0);
  expect_no_frame();
}

ZTEST(features, test_0x25_heartbeat_is_counted_and_echoed)
{
  g1_send_vector("0x25_teleprompter_heartbeat");
  g1_send_vector("0x25_teleprompter_heartbeat");

  zassert_equal(tp_app_sync_count, 2);
  expect_frame("25 06 00 04 04 01");
  expect_only_frame("25 06 00 04 04 01");
}
