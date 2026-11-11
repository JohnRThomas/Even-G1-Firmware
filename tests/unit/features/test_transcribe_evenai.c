/*
 * 0x52 transcribe control and 0x53 heartbeat. 0x52 sub 1 mode: 0 stop, 1 start, 2 start without
 * clock and icon, 3 pause, 4 resume; sub 2 text; sub 3 status. flags bit 0 animates the icon.
 * Even AI: 0x54 answer text, 0x55 heartbeat, 0x56 exit.
 */
#include "features.h"

#define CHUNK_HDR 12    /* 0x52 sub 2 and 0x54 sub 3 header */
#define TEXT_MAX  0x200 /* sizeof(transcribe_buf.data), sizeof(evenai_buf.data) */
#define TR_BLOCK  0x208 /* allocated block size */
/* the largest chunk the link delivers, and what two of them leave of TEXT_MAX */
#define CHUNK_MAX (0x100 - CHUNK_HDR)
#define TAIL      (TEXT_MAX - 2 * CHUNK_MAX)

static struct transcribe_buf *tr(void)
{
  return g1_state()->transcribe_buf;
}

static struct evenai_buf *ai(void)
{
  return g1_state()->evenai_buf;
}

/* the state sub 1 mode 1 / 2 leaves */
static void tr_running(uint8_t mode)
{
  tr()->running = true;
  tr()->mode = mode;
  tr()->flags = mode == 2 ? 0x00 : 0xff;
}

/*
 * 0x52 sub 2 / 0x54 sub 3: chunk header, status, two bytes (0x52: icon off, dark lines; 0x54:
 * scroll line, scroll %), text
 */
static void send_chunk(uint8_t cmd, uint8_t seq, uint16_t total, uint16_t index, uint8_t status,
           uint8_t byte10, uint8_t byte11, const void *text, size_t len)
{
  uint8_t pkt[0x100];
  uint8_t payload[sizeof(pkt) - G1_CHUNK_HDR_LEN] = {status, byte10, byte11};
  uint8_t sub = cmd == 0x52 ? 0x02 : 0x03;

  memcpy(&payload[3], text, len);
  g1_send(pkt, g1_put_chunk(pkt, cmd, seq, sub, total, index, payload, 3 + len));
}

static void send_transcribe_text(uint8_t seq, uint16_t total, uint16_t index, uint8_t status,
         uint8_t icon_off, uint8_t dark_lines, const void *text, size_t len)
{
  send_chunk(0x52, seq, total, index, status, icon_off, dark_lines, text, len);
}

static void send_evenai_answer(uint8_t seq, uint16_t total, uint16_t index, uint8_t status,
             uint8_t scroll_line, uint8_t scroll_pct, const void *text,
             size_t len)
{
  send_chunk(0x54, seq, total, index, status, scroll_line, scroll_pct, text, len);
}

/* transcribe text with status, icon off and dark lines 0 */
static void send_transcribe_str(uint8_t seq, uint16_t total, uint16_t index, const char *text)
{
  send_transcribe_text(seq, total, index, 0, 0, 0, text, strlen(text));
}

/* sends a chunk with status and bytes 10 and 11 zero and checks its reply */
static void send_chunk_expect_reply(uint8_t cmd, uint8_t seq, uint16_t total, uint16_t index,
            const void *text, size_t len, const char *reply)
{
  send_chunk(cmd, seq, total, index, 0, 0, 0, text, len);
  expect_frame(reply);
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
  memcpy(tr()->data, "old text", 8);
  tr()->len = 8;
  tr()->status = 3;
  tr()->dark_lines = 4;

  g1_send_hex("52 06 00 05 01 01");

  zassert_equal(tr()->len, 0);
  zassert_equal(tr()->status, 0);
  zassert_equal(tr()->dark_lines, 0);
  expect_fill(tr()->data, TEXT_MAX, 0, "text");
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
  tr_running(1);
  send_transcribe_text(0x10, 1, 1, 0x02, 0x00, 0x03, "Hello world", 11);

  feat_expect_field(tr()->data, TEXT_MAX, "Hello world");
  zassert_equal(tr()->len, 11);
  zassert_equal(tr()->status, 0x02);
  zassert_equal(tr()->dark_lines, 0x03);
  expect_only_frame("52 0a 00 10 02 01 00 01 00 c9");
}

ZTEST(features, test_0x52_text_sets_the_esb_down_id)
{
  tr_running(1);
  g1_state()->esb_down_recving = 1;

  send_transcribe_str(0x11, 1, 1, "abc");

  zassert_equal(g1_state()->esb_down_id, 0x11);
  zassert_equal(g1_state()->esb_down_recving, 0);
}

/* runs in mode with the icon flag at flags_before and sends a text with byte 10 = icon_off */
#define ICON_FLAG_TEST(name, mode, flags_before, icon_off, flags_after)                            \
  ZTEST(features, test_##name)                                                               \
  {                                                                                          \
    tr_running(mode);                                                                  \
    tr()->flags = flags_before;                                                        \
    send_transcribe_text(0x12, 1, 1, 0, icon_off, 0, "abc", 3);                        \
    zassert_equal(tr()->flags, flags_after);                                           \
  }
ICON_FLAG_TEST(0x52_text_byte_10_01_clears_the_icon_flag, 1, 0xff, 0x01, 0xfe)
ICON_FLAG_TEST(0x52_text_byte_10_00_sets_the_icon_flag, 1, 0xfe, 0x00, 0xff)
ICON_FLAG_TEST(0x52_text_in_mode_2_keeps_the_icon_flag_clear, 2, 0x00, 0x00, 0x00)

ZTEST(features, test_0x52_text_of_only_the_header_stores_an_empty_text)
{
  tr_running(1);
  send_transcribe_str(0x15, 1, 1, "old");
  g1_frames_clear();

  g1_send_hex("52 0c 00 16 02 01 00 01 00 00 00 00");

  zassert_equal(tr()->len, 0);
  feat_expect_field(tr()->data, TEXT_MAX, "");
  expect_frame("52 0a 00 16 02 01 00 01 00 c9");
}

ZTEST(features, test_0x52_text_shorter_than_its_header_is_refused)
{
  tr_running(1);
  g1_send_hex("52 0b 00 17 02 01 00 01 00 00 00");

  expect_frame("52 0a 00 17 02 01 00 01 00 ca");
  zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_in_two_chunks_is_assembled)
{
  tr_running(1);
  send_transcribe_text(0x20, 2, 1, 0x01, 0x00, 0x01, "Hello ", 6);
  expect_frame("52 0a 00 20 02 02 00 01 00 c9");
  zassert_equal(tr()->len, 0, "text shown before the last chunk");

  send_transcribe_text(0x21, 2, 2, 0x04, 0x01, 0x05, "world", 5);
  expect_frame("52 0a 00 21 02 02 00 02 00 c9");

  feat_expect_field(tr()->data, TEXT_MAX, "Hello world");
  zassert_equal(tr()->len, 11);
  zassert_equal(tr()->status, 0x04);
  zassert_equal(tr()->dark_lines, 0x05);
  zassert_equal(tr()->flags, 0xfe);
  zassert_equal(g1_state()->esb_down_id, 0x21);
}

/* 244 + 244 + 24 = 0x200 */
ZTEST(features, test_0x52_text_of_0x200_bytes_is_accepted)
{
  static uint8_t text[TEXT_MAX];

  for (size_t offset = 0; offset < sizeof(text); offset++) {
    text[offset] = 'a' + offset % 26;
  }
  feat_guard_block(&g1_state()->transcribe_buf, TR_BLOCK, 0, 0x40);
  tr_running(1);

  send_chunk_expect_reply(0x52, 0x30, 3, 1, text, CHUNK_MAX, "52 0a 00 30 02 03 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x31, 3, 2, text + CHUNK_MAX, CHUNK_MAX,
        "52 0a 00 31 02 03 00 02 00 c9");
  send_chunk_expect_reply(0x52, 0x32, 3, 3, text + 2 * CHUNK_MAX, TAIL,
        "52 0a 00 32 02 03 00 03 00 c9");
  zassert_equal(tr()->len, TEXT_MAX);
  zassert_mem_equal(tr()->data, text, TEXT_MAX);
  feat_expect_guard_intact();
}

ZTEST(features, test_0x52_text_of_0x201_bytes_is_refused)
{
  static uint8_t text[TEXT_MAX + 1];

  memset(text, 'z', sizeof(text));
  tr_running(1);

  send_chunk_expect_reply(0x52, 0x30, 3, 1, text, CHUNK_MAX, "52 0a 00 30 02 03 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x31, 3, 2, text, CHUNK_MAX, "52 0a 00 31 02 03 00 02 00 c9");
  send_chunk_expect_reply(0x52, 0x32, 3, 3, text, TAIL + 1, "52 0a 00 32 02 03 00 03 00 ca");
  zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_middle_chunk_reaching_0x200_is_accepted)
{
  static uint8_t text[TEXT_MAX];

  memset(text, 'm', sizeof(text));
  tr_running(1);

  send_chunk_expect_reply(0x52, 0x40, 4, 1, text, CHUNK_MAX, "52 0a 00 40 02 04 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x41, 4, 2, text, CHUNK_MAX, "52 0a 00 41 02 04 00 02 00 c9");
  send_chunk_expect_reply(0x52, 0x42, 4, 3, text, TAIL, "52 0a 00 42 02 04 00 03 00 c9");
  send_chunk_expect_reply(0x52, 0x43, 4, 4, text, 0, "52 0a 00 43 02 04 00 04 00 c9");
  zassert_equal(tr()->len, TEXT_MAX);
}

ZTEST(features, test_0x52_text_middle_chunk_beyond_0x200_is_refused)
{
  static uint8_t text[TEXT_MAX + 1];

  memset(text, 'm', sizeof(text));
  tr_running(1);

  send_chunk_expect_reply(0x52, 0x40, 4, 1, text, CHUNK_MAX, "52 0a 00 40 02 04 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x41, 4, 2, text, CHUNK_MAX, "52 0a 00 41 02 04 00 02 00 c9");
  send_chunk_expect_reply(0x52, 0x42, 4, 3, text, TAIL + 1, "52 0a 00 42 02 04 00 03 00 ca");
  zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_last_chunk_out_of_order_replies_ca)
{
  tr_running(1);
  send_chunk_expect_reply(0x52, 0x50, 3, 1, "one ", 4, "52 0a 00 50 02 03 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x51, 3, 3, "three", 5, "52 0a 00 51 02 03 00 03 00 ca");

  zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_middle_chunk_out_of_order_replies_ca)
{
  tr_running(1);
  send_chunk_expect_reply(0x52, 0x52, 4, 1, "one ", 4, "52 0a 00 52 02 04 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x53, 4, 3, "three ", 6, "52 0a 00 53 02 04 00 03 00 ca");

  zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_last_chunk_of_another_total_replies_ca)
{
  tr_running(1);
  send_chunk_expect_reply(0x52, 0x54, 3, 1, "one ", 4, "52 0a 00 54 02 03 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x55, 2, 2, "two", 3, "52 0a 00 55 02 02 00 02 00 ca");

  zassert_equal(tr()->len, 0);
}

ZTEST(features, test_0x52_text_chunk_index_beyond_total_is_ignored)
{
  tr_running(1);
  send_transcribe_str(0x56, 2, 1, "abc");
  send_transcribe_str(0x57, 2, 3, "zzz");
  send_transcribe_str(0x58, 2, 2, "def");

  feat_expect_field(tr()->data, TEXT_MAX, "abcdef");
  zassert_equal(tr()->len, 6);
}

ZTEST(features, test_0x52_text_chunk_index_beyond_total_replies_ca)
{
  tr_running(1);
  send_chunk_expect_reply(0x52, 0x56, 2, 1, "abc", 3, "52 0a 00 56 02 02 00 01 00 c9");
  send_chunk_expect_reply(0x52, 0x57, 2, 3, "zzz", 3, "52 0a 00 57 02 02 00 03 00 ca");
}

ZTEST(features, test_0x52_single_text_inside_a_chunked_text_does_not_corrupt_it)
{
  tr_running(1);
  send_transcribe_str(0x60, 2, 1, "abc");
  send_transcribe_str(0x61, 1, 1, "x");
  send_transcribe_str(0x62, 2, 2, "def");

  zassert_equal(strnlen((const char *)tr()->data, TEXT_MAX), tr()->len,
          "text has a NUL inside its length %u", tr()->len);
}

ZTEST(features, test_0x52_text_chunk_shorter_than_its_header_is_refused)
{
  tr_running(1);
  g1_send_hex("52 0b 00 18 02 02 00 01 00 00 00");

  expect_frame("52 0a 00 18 02 02 00 01 00 ca");
}

ZTEST(features, test_0x52_resets_the_screen_timeout)
{
  tr_running(1);

  g1_send_hex("52 06 00 1a 01 03");
  send_transcribe_str(0x1b, 1, 1, "abc");
  g1_send_hex("52 06 00 1c 03 00");

  zassert_equal(transcribe_info_timeout_reset_fake.call_count, 3);
}

ZTEST(features, test_0x52_unknown_sub_replies_ca)
{
  g1_send_hex("52 06 00 1d 00 01");
  g1_send_hex("52 06 00 1e 04 01");

  expect_frame("52 06 00 1d 00 ca");
  expect_only_frame("52 06 00 1e 04 ca");
  zassert_false(tr()->running);
  zassert_equal(update_persist_task_status_fake.call_count, 0);
}

ZTEST(features, test_0x52_length_field_mismatch_replies_ca)
{
  g1_send_hex("52 07 00 1f 01 01");

  expect_only_frame("52 06 00 1f 01 ca");
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
  send_transcribe_str(0x22, 1, 1, "kept");
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
  expect_only_frame("53 06 00 25 04 ca");
  zassert_true(tr()->running);
}

ZTEST(features, test_0x53_length_field_mismatch_replies_ca)
{
  tr_running(1);

  g1_send_hex("53 07 00 26 03 01");

  expect_only_frame("53 06 00 26 03 ca");
  zassert_equal(transcribe_info_timeout_reset_fake.call_count, 0);
}

ZTEST(features, test_0x54_answer_text_is_stored_in_evenai_buf)
{
  g1_send_vector("0x54_evenai_text");

  zassert_equal(ai()->sub, 3);
  zassert_equal(ai()->action_status, 0);
  zassert_equal(ai()->scroll_line, 0);
  zassert_equal(ai()->scroll_pct, 100);
  zassert_equal(ai()->len, 31);
  feat_expect_field(ai()->data, TEXT_MAX, "Paris is the capital of France.");
  expect_only_frame("54 0a 00 00 03 01 00 01 00 c9");
}

ZTEST(features, test_0x54_answer_replaces_the_previous_text)
{
  g1_send_vector("0x54_evenai_text");
  send_evenai_answer(0x01, 1, 1, 0, 0, 50, "Yes.", 4);

  feat_expect_field(ai()->data, TEXT_MAX, "Yes.");
  zassert_equal(ai()->len, 4);
  zassert_equal(ai()->scroll_pct, 50);
}

ZTEST(features, test_0x54_answer_in_two_chunks_is_assembled)
{
  send_evenai_answer(0x10, 2, 1, 0x01, 0x02, 10, "Hello ", 6);
  expect_frame("54 0a 00 10 03 02 00 01 00 c9");
  zassert_equal(ai()->len, 0, "text shown before the last chunk");

  send_evenai_answer(0x11, 2, 2, 0x00, 0x03, 90, "world", 5);
  expect_frame("54 0a 00 11 03 02 00 02 00 c9");

  feat_expect_field(ai()->data, TEXT_MAX, "Hello world");
  zassert_equal(ai()->len, 11);
  zassert_equal(ai()->scroll_line, 3);
  zassert_equal(ai()->scroll_pct, 90);
}

/* 244 + 244 + 24 = 0x200 */
ZTEST(features, test_0x54_answer_of_0x200_bytes_is_accepted)
{
  static uint8_t text[TEXT_MAX];

  for (size_t offset = 0; offset < sizeof(text); offset++) {
    text[offset] = 'a' + offset % 26;
  }

  send_chunk_expect_reply(0x54, 0x20, 3, 1, text, CHUNK_MAX, "54 0a 00 20 03 03 00 01 00 c9");
  send_chunk_expect_reply(0x54, 0x21, 3, 2, text + CHUNK_MAX, CHUNK_MAX,
        "54 0a 00 21 03 03 00 02 00 c9");
  send_chunk_expect_reply(0x54, 0x22, 3, 3, text + 2 * CHUNK_MAX, TAIL,
        "54 0a 00 22 03 03 00 03 00 c9");
  zassert_equal(ai()->len, TEXT_MAX);
  zassert_mem_equal(ai()->data, text, TEXT_MAX);
}

ZTEST(features, test_0x54_answer_of_0x201_bytes_is_refused)
{
  static uint8_t text[TEXT_MAX + 1];

  memset(text, 'z', sizeof(text));

  send_chunk_expect_reply(0x54, 0x20, 3, 1, text, CHUNK_MAX, "54 0a 00 20 03 03 00 01 00 c9");
  send_chunk_expect_reply(0x54, 0x21, 3, 2, text, CHUNK_MAX, "54 0a 00 21 03 03 00 02 00 c9");
  send_chunk_expect_reply(0x54, 0x22, 3, 3, text, TAIL + 1, "54 0a 00 22 03 03 00 03 00 ca");
  zassert_equal(ai()->len, 0);
}

ZTEST(features, test_0x54_length_field_mismatch_replies_ca)
{
  g1_send_vector_with("0x54_evenai_text", 1, 0x2c); /* length 0x2b + 1 */

  expect_frame("54 06 00 00 03 ca");
  zassert_equal(ai()->len, 0);
}

ZTEST(features, test_0x55_heartbeat_while_active_replies_c9)
{
  ai()->state = 1;
  g1_send_vector("0x55_evenai_heartbeat");

  expect_only_frame("55 07 00 01 05 c9 00");
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
