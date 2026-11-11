/* 0xF1 audio: F1 <seq> <200-byte block>. Fake mic queue: block k is filled with fill + k. */
#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "ble/ble_proto.h"

#define AUDIO_BLOCK 200            /* DMIC_MSG_SIZE */
#define AUDIO_FRAME (2 + AUDIO_BLOCK)

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

static void queue_audio(uint32_t n, uint8_t fill)
{
	get_dmic_num_messages_fake.custom_fake = fake_dmic_num_messages;
	dequeue_dmic_fake.custom_fake = fake_dequeue_dmic;
	blocks_queued = n;
	next_fill = fill;
}

static void expect_audio_frame(uint8_t seq, uint8_t fill)
{
	uint8_t got[AUDIO_FRAME + 16];
	int n = g1_frame_pop(got, sizeof(got));

	zassert_equal(n, AUDIO_FRAME, "audio frame of %d bytes (seq 0x%02x expected)", n, seq);
	zassert_equal(got[0], 0xf1, "byte 0 = 0x%02x", got[0]);
	zassert_equal(got[1], seq, "sequence 0x%02x, want 0x%02x", got[1], seq);
	for (int i = 2; i < AUDIO_FRAME; i++) {
		zassert_equal(got[i], fill, "seq 0x%02x byte %d = 0x%02x, want 0x%02x", seq, i,
			      got[i], fill);
	}
}

static void send_all_audio(void)
{
	for (int calls = 0; blocks_queued && calls < 300; calls++) {
		zassert_equal(send_dmic_msg(g1_ble()), 0);
	}
	zassert_equal(blocks_queued, 0, "blocks left after 300 calls");
}

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
