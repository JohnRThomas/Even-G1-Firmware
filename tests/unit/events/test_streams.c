/*
 * Streams the BLE work thread drains. 0xF1 audio: F1 <seq> <200-byte block>; the fake mic queue
 * fills block k with fill + k. 0xF4 debug channel: F4 <text> 00, a queue of 20 lines of 200 bytes.
 */
#include <errno.h>

#include "fakes.h"
#include "ble/ble_proto.h"

#define AUDIO_BLOCK 200 /* DMIC_MSG_SIZE */

extern int debug_pending;

static uint32_t blocks_queued;
static uint8_t next_fill;

static uint32_t fake_dmic_num_messages(void)
{
  return blocks_queued;
}

static int fake_dequeue_dmic(void *msg)
{
  if (blocks_queued == 0) {
    return -ENOMSG;
  }
  memset(msg, next_fill++, AUDIO_BLOCK);
  blocks_queued--;
  return 0;
}

static void queue_audio(uint32_t count, uint8_t fill)
{
  get_dmic_num_messages_fake.custom_fake = fake_dmic_num_messages;
  dequeue_dmic_fake.custom_fake = fake_dequeue_dmic;
  blocks_queued = count;
  next_fill = fill;
}

static void expect_audio_frame(uint8_t seq, uint8_t fill)
{
  uint8_t want[2 + AUDIO_BLOCK] = {0xf1, seq};

  memset(&want[2], fill, AUDIO_BLOCK);
  expect_frame_bytes(want, sizeof(want));
}

static void send_all_audio(void)
{
  for (int calls = 0; blocks_queued && calls < 300; calls++) {
    zassert_equal(send_dmic_msg(g1_ble()), 0);
  }
  zassert_equal(blocks_queued, 0, "blocks left after 300 calls");
}

static void expect_debug_frame(const char *text)
{
  uint8_t want[DEBUG_MSG_SIZE + 2] = {0xf4};

  strcpy((char *)&want[1], text);
  expect_frame_bytes(want, strlen(text) + 2);
}

/* queues "line <number>\n" for number 0 .. count - 1 */
static void queue_debug_lines(int count)
{
  for (int number = 0; number < count; number++) {
    ble_printk("line %d\n", number);
  }
}

/* expects count frames from "line <first>\n" on, and then no further frame */
static void expect_debug_lines(int first, int count)
{
  char line[16];

  for (int number = first; number < first + count; number++) {
    snprintf(line, sizeof(line), "line %d\n", number);
    expect_debug_frame(line);
  }
  expect_no_frame();
}

/* ---- 0xF1 audio ---- */

ZTEST(events, test_audio_block_is_sent_as_f1_seq_and_200_bytes_202_in_total)
{
  queue_audio(1, 0x5a);

  zassert_equal(send_dmic_msg(g1_ble()), 0);

  expect_audio_frame(0x00, 0x5a);
  expect_no_frame();
}

ZTEST(events, test_audio_sequence_wraps_from_0xff_to_0x00)
{
  uint8_t want = 0;
  int sent = 0;

  queue_audio(258, 0);
  while (blocks_queued) {
    zassert_equal(send_dmic_msg(g1_ble()), 0);
    while (g1_frame_count()) {
      expect_audio_frame(want, (uint8_t)sent);
      want++;
      sent++;
    }
  }
  zassert_equal(sent, 258);
  zassert_equal(want, 0x02, "the last two packets are FF 00 01 ... 0x%02x", want);
}

ZTEST(events, test_no_audio_queued_sends_nothing)
{
  queue_audio(0, 0);

  zassert_equal(send_dmic_msg(g1_ble()), 0);

  expect_no_frame();
}

ZTEST(events, test_failed_dequeue_returns_its_error_and_sends_nothing)
{
  get_dmic_num_messages_fake.return_val = 1;
  dequeue_dmic_fake.return_val = -EIO;

  zassert_equal(send_dmic_msg(g1_ble()), -EIO);

  expect_no_frame();
}

ZTEST(events, test_failed_dequeue_does_not_advance_the_sequence)
{
  get_dmic_num_messages_fake.return_val = 1;
  dequeue_dmic_fake.return_val = -EIO;
  send_dmic_msg(g1_ble());

  queue_audio(1, 0x33);
  send_dmic_msg(g1_ble());

  expect_audio_frame(0x00, 0x33);
}

ZTEST(events, test_f1_cc_packet_through_the_dispatcher_sends_the_queued_audio)
{
  queue_audio(2, 0x40);

  g1_send_hex("f1 00 c9");
  g1_send_hex("f1 00 cb");
  expect_no_frame();

  g1_send_hex("f1 00 cc");
  expect_audio_frame(0x00, 0x40);
  expect_audio_frame(0x01, 0x41);
  expect_no_frame();
}

/* DEFECT: 0x0E does not reset dmic_seq, so the sequence continues. */
ZTEST_EXPECT_FAIL(events, test_audio_sequence_restarts_at_0_when_the_mic_is_switched_on_again);
ZTEST(events, test_audio_sequence_restarts_at_0_when_the_mic_is_switched_on_again)
{
  queue_audio(3, 0x20);
  send_all_audio();
  expect_audio_frame(0x00, 0x20);
  expect_audio_frame(0x01, 0x21);
  expect_audio_frame(0x02, 0x22);

  g1_send_vector("0x0e_mic_off");
  g1_send_vector("0x0e_mic_on");
  expect_frame_padded("0e c9 00", 20);
  expect_frame_padded("0e c9 01", 20);
  zassert_equal(request_dmic_stream_start_fake.call_count, 1, "mic not switched on");

  queue_audio(1, 0x30);
  send_dmic_msg(g1_ble());
  expect_audio_frame(0x00, 0x30);
}

/* ---- 0xF4 debug ---- */

ZTEST(events, test_debug_line_is_sent_as_f4_text_and_nul)
{
  ble_printk("battery %d%%\n", 87);

  zassert_equal(send_debug_msg(g1_ble()), 0);

  expect_only_frame("f4 62 61 74 74 65 72 79 20 38 37 25 0a 00"); /* "battery 87%\n" */
}

ZTEST(events, test_send_debug_msg_with_an_empty_queue_sends_nothing)
{
  zassert_equal(send_debug_msg(g1_ble()), 0);
  expect_no_frame();
}

ZTEST(events, test_debug_queue_holds_20_lines_and_the_21st_drops_the_oldest)
{
  queue_debug_lines(DEBUG_MSGQ_MAX + 1);

  send_debug_msg(g1_ble());

  expect_debug_lines(1, DEBUG_MSGQ_MAX);
}

ZTEST(events, test_debug_queue_full_of_20_lines_drops_nothing)
{
  queue_debug_lines(DEBUG_MSGQ_MAX);

  send_debug_msg(g1_ble());

  expect_debug_lines(0, DEBUG_MSGQ_MAX);
}

ZTEST(events, test_debug_line_longer_than_199_characters_is_cut_to_199_and_nul)
{
  char text[251];

  memset(text, 'x', 250);
  text[250] = '\0';
  ble_printk("%s", text);

  send_debug_msg(g1_ble());

  text[199] = '\0';
  expect_debug_frame(text);
  expect_no_frame();
}

ZTEST(events, test_debug_line_wakes_the_idle_ble_work_thread)
{
  struct ble_context *ctx = g1_ble();

  ctx->receiver_thd_is_running = 0;
  ble_printk("a\n");
  zassert_equal(debug_pending, 1);
  zassert_equal(k_sem_count_get(&ctx->rx_sem), 1);

  ble_printk("b\n");
  zassert_equal(k_sem_count_get(&ctx->rx_sem), 1, "given once, not per line");
}

ZTEST(events, test_debug_line_does_not_wake_the_running_ble_work_thread)
{
  struct ble_context *ctx = g1_ble();

  ctx->receiver_thd_is_running = 1;
  ble_printk("a\n");

  zassert_equal(debug_pending, 0);
  zassert_equal(k_sem_count_get(&ctx->rx_sem), 0);
}

ZTEST(events, test_f4_packet_from_the_work_thread_sends_the_queued_lines)
{
  ble_printk("hello\n");

  g1_send_hex("f4 00 cb");

  expect_only_frame("f4 68 65 6c 6c 6f 0a 00");
}

ZTEST(events, test_firmware_log_line_goes_to_the_phone_when_debug_logging_is_on)
{
  BLE_DEBUG = 1;

  g1_send_hex("60");
  expect_only_frame("4e 4e 00");

  send_debug_msg(g1_ble());
  expect_debug_frame("ble_process_req_dispatch(): tx error req_type. 96\n");
  expect_no_frame();
}

ZTEST(events, test_firmware_log_line_stays_off_the_phone_when_debug_logging_is_off)
{
  g1_send_hex("60");
  expect_frame("4e 4e 00");

  send_debug_msg(g1_ble());
  expect_no_frame();
}

ZTEST(events, test_debug_logging_set_23_6c_00_enables_the_channel)
{
  g1_send_hex("23 6c 00");

  zassert_equal(BLE_DEBUG, 1);
  expect_no_frame();
}

/* DEFECT: "#l" disables only when atoi() of the arg is 1 (ASCII '1'), so C1 enables. */
ZTEST_EXPECT_FAIL(events, test_debug_logging_set_23_6c_c1_disables_the_channel);
ZTEST(events, test_debug_logging_set_23_6c_c1_disables_the_channel)
{
  BLE_DEBUG = 1;

  g1_send_hex("23 6c c1");

  zassert_equal(BLE_DEBUG, 0, "BLE_DEBUG %d after 23 6C C1", BLE_DEBUG);
}
