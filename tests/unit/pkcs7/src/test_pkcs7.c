/* PKCS#7 padding: RFC 5652 section 6.3. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "ble/ble_proto.h"

#define WHITELIST_BUF_SIZE 0x1800
#define NCS_BUF_SIZE       0x800
#define GUARD_SIZE         0x400
#define SENTINEL           '~'    /* printable: the msg variant printk()s dst */
#define SENTINEL_LEN       64     /* then NULs, so %s stops */
#define APP_CHUNK_PAYLOAD  180

static uint8_t dst[WHITELIST_BUF_SIZE + GUARD_SIZE];

static void pkcs7_before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(dst, 0, sizeof(dst));
	memset(dst, SENTINEL, SENTINEL_LEN);
}

ZTEST_SUITE(pkcs7, NULL, NULL, pkcs7_before, NULL, NULL);

static void expect_bytes(const uint8_t *buf, size_t len, const char *hex)
{
	uint8_t want[256];
	char got_txt[800];
	int n = g1_hex_parse(hex, want, sizeof(want));

	zassert_true(n >= 0, "bad hex in test: %s", hex);
	zassert_equal(len, (size_t)n, "test compares %u bytes with %d bytes of hex",
		      (unsigned int)len, n);
	g1_hex_format(buf, len, got_txt, sizeof(got_txt));
	zassert_mem_equal(buf, want, len, "want %s\n got %s", hex, got_txt);
}

static void expect_dst(const char *hex)
{
	uint8_t want[256];
	int n = g1_hex_parse(hex, want, sizeof(want));

	zassert_true(n > 0, "bad hex in test: %s", hex);
	expect_bytes(dst, n, hex);
}

typedef int (*parse_fn)(uint8_t *origin_data, const uint8_t *req, uint16_t req_len);

static void expect_parse(parse_fn fn, const char *name, const char *hex, int want)
{
	uint8_t req[256];
	int n = g1_hex_parse(hex, req, sizeof(req));
	int got;

	zassert_true(n >= 0, "bad hex in test: %s", hex);
	got = fn(dst, req, (uint16_t)n);
	zassert_equal(got, want, "%s(%s) = 0x%02x, want 0x%02x", name, hex, got, want);
}

static void expect_pack(const char *hex, int want)
{
	expect_parse(parse_receiver_pack_pkcs7, "parse_receiver_pack_pkcs7", hex, want);
}

static void expect_msg(const char *hex, int want)
{
	expect_parse(parse_receiver_msg_pack_pkcs7, "parse_receiver_msg_pack_pkcs7", hex, want);
}

static void expect_untouched(size_t from, size_t len)
{
	for (size_t i = from; i < from + len; i++) {
		zassert_equal(dst[i], SENTINEL, "dst[0x%x] = 0x%02x was written (checked from 0x%x)",
			      (unsigned int)i, dst[i], (unsigned int)from);
	}
}

/* ---- pack ---- */

ZTEST(pkcs7, test_pack_single_chunk_replies_c9_and_copies_the_payload)
{
	expect_pack("04 01 00 7b 7d", PKCS7_PACK_DONE);
	expect_dst("7b 7d 7e");
}

ZTEST(pkcs7, test_pack_in_order_chunks_reply_cb_then_c9_and_concatenate)
{
	expect_pack("04 03 00 41 42", PKCS7_PACK_MORE);
	expect_pack("04 03 01 43", PKCS7_PACK_MORE);
	expect_pack("04 03 02 44 45", PKCS7_PACK_DONE);
	expect_dst("41 42 43 44 45 7e");
}

ZTEST(pkcs7, test_pack_index_0_restarts_at_offset_0)
{
	expect_pack("04 03 00 41 42", PKCS7_PACK_MORE);
	expect_pack("04 03 01 43 44", PKCS7_PACK_MORE);
	expect_pack("04 02 00 58", PKCS7_PACK_MORE);
	expect_pack("04 02 01 59", PKCS7_PACK_DONE);
	expect_dst("58 59");
}

ZTEST(pkcs7, test_pack_index_0_takes_the_new_command)
{
	expect_pack("04 03 00 41", PKCS7_PACK_MORE);
	expect_pack("05 02 00 31", PKCS7_PACK_MORE);
	expect_pack("05 02 01 32", PKCS7_PACK_DONE);
	expect_dst("31 32 7e");
}

ZTEST(pkcs7, test_pack_command_change_mid_sequence_replies_ca_and_writes_nothing)
{
	expect_pack("04 03 00 41", PKCS7_PACK_MORE);
	expect_pack("05 03 01 42", PKCS7_PACK_ERR);
	expect_dst("41 7e");
}

ZTEST(pkcs7, test_pack_request_shorter_than_3_bytes_replies_ca)
{
	static const uint8_t req[3] = {0x04, 0x01, 0x00};

	for (uint16_t len = 0; len < 3; len++) {
		int got = parse_receiver_pack_pkcs7(dst, req, len);

		zassert_equal(got, PKCS7_PACK_ERR, "req_len %u: 0x%02x", len, got);
	}
	expect_untouched(0, SENTINEL_LEN);
}

ZTEST(pkcs7, test_pack_header_only_chunk_is_accepted)
{
	expect_pack("04 02 00", PKCS7_PACK_MORE);
	expect_pack("04 02 01 41", PKCS7_PACK_DONE);
	expect_dst("41 7e");
}

ZTEST(pkcs7, test_pack_null_request_replies_ca)
{
	zassert_equal(parse_receiver_pack_pkcs7(dst, NULL, 5), PKCS7_PACK_ERR);
	expect_untouched(0, SENTINEL_LEN);
}

ZTEST(pkcs7, test_pack_null_destination_replies_ca)
{
	static const uint8_t req[] = {0x04, 0x01, 0x00, 0x41};

	zassert_equal(parse_receiver_pack_pkcs7(NULL, req, sizeof(req)), PKCS7_PACK_ERR);
}

/* DEFECT: no capacity check; chunk 34 overruns the buffer (dst has a guard). */
ZTEST_EXPECT_FAIL(pkcs7, test_pack_chunk_overrunning_the_whitelist_buffer_is_rejected);
ZTEST(pkcs7, test_pack_chunk_overrunning_the_whitelist_buffer_is_rejected)
{
	uint8_t req[3 + APP_CHUNK_PAYLOAD];
	int got;

	memset(dst + WHITELIST_BUF_SIZE, SENTINEL, GUARD_SIZE);
	req[0] = 0x04;
	req[1] = 40;
	memset(req + 3, 'j', APP_CHUNK_PAYLOAD);
	for (int idx = 0; idx < 34; idx++) {
		req[2] = idx;
		got = parse_receiver_pack_pkcs7(dst, req, sizeof(req));
		zassert_equal(got, PKCS7_PACK_MORE, "chunk %d (fits): 0x%02x", idx, got);
	}

	req[2] = 34;
	got = parse_receiver_pack_pkcs7(dst, req, sizeof(req));

	expect_untouched(WHITELIST_BUF_SIZE, GUARD_SIZE);
	zassert_equal(got, PKCS7_PACK_ERR, "chunk 34 (ends at %d): 0x%02x", 35 * 180, got);
}

/* DEFECT: a resent chunk (lost reply) is appended again. */
ZTEST_EXPECT_FAIL(pkcs7, test_pack_duplicated_chunk_index_is_not_appended_twice);
ZTEST(pkcs7, test_pack_duplicated_chunk_index_is_not_appended_twice)
{
	uint8_t req[] = {0x04, 0x03, 0x01, 0x42};

	expect_pack("04 03 00 41", PKCS7_PACK_MORE);
	expect_pack("04 03 01 42", PKCS7_PACK_MORE);
	/* its reply is not asserted */
	parse_receiver_pack_pkcs7(dst, req, sizeof(req));
	expect_pack("04 03 02 43", PKCS7_PACK_DONE);
	expect_dst("41 42 43 7e");
}

/* ---- msg ---- */

ZTEST(pkcs7, test_msg_single_chunk_replies_c9_and_copies_the_payload)
{
	expect_msg("4b 00 01 00 7b 7d", PKCS7_PACK_DONE);
	expect_dst("7b 7d 7e");
}

ZTEST(pkcs7, test_msg_in_order_chunks_reply_cb_then_c9_and_concatenate)
{
	expect_msg("4b 00 03 00 41 42", PKCS7_PACK_MORE);
	expect_msg("4b 00 03 01 43", PKCS7_PACK_MORE);
	expect_msg("4b 00 03 02 44 45", PKCS7_PACK_DONE);
	expect_dst("41 42 43 44 45 7e");
}

ZTEST(pkcs7, test_msg_index_0_restarts_at_offset_0)
{
	expect_msg("4b 00 03 00 41 42", PKCS7_PACK_MORE);
	expect_msg("4b 00 03 01 43 44", PKCS7_PACK_MORE);
	expect_msg("4b 00 02 00 58", PKCS7_PACK_MORE);
	expect_msg("4b 00 02 01 59", PKCS7_PACK_DONE);
	expect_dst("58 59");
}

ZTEST(pkcs7, test_msg_index_0_takes_the_new_command_and_message_id)
{
	expect_msg("4b 01 03 00 41", PKCS7_PACK_MORE);
	expect_msg("4c 02 02 00 31", PKCS7_PACK_MORE);
	expect_msg("4c 02 02 01 32", PKCS7_PACK_DONE);
	expect_dst("31 32 7e");
}

ZTEST(pkcs7, test_msg_command_change_mid_sequence_replies_ca_and_writes_nothing)
{
	expect_msg("4b 00 03 00 41", PKCS7_PACK_MORE);
	expect_msg("4c 00 03 01 42", PKCS7_PACK_ERR);
	expect_dst("41 7e");
}

ZTEST(pkcs7, test_msg_message_id_change_mid_sequence_replies_ca_and_writes_nothing)
{
	expect_msg("4b 01 03 00 41", PKCS7_PACK_MORE);
	expect_msg("4b 02 03 01 42", PKCS7_PACK_ERR);
	expect_dst("41 7e");
}

ZTEST(pkcs7, test_msg_request_shorter_than_4_bytes_replies_ca)
{
	static const uint8_t req[4] = {0x4b, 0x00, 0x01, 0x00};

	for (uint16_t len = 0; len < 4; len++) {
		int got = parse_receiver_msg_pack_pkcs7(dst, req, len);

		zassert_equal(got, PKCS7_PACK_ERR, "req_len %u: 0x%02x", len, got);
	}
	expect_untouched(0, SENTINEL_LEN);
}

ZTEST(pkcs7, test_msg_header_only_chunk_is_accepted)
{
	expect_msg("4b 00 02 00", PKCS7_PACK_MORE);
	expect_msg("4b 00 02 01 41", PKCS7_PACK_DONE);
	expect_dst("41 7e");
}

ZTEST(pkcs7, test_msg_null_request_replies_ca)
{
	zassert_equal(parse_receiver_msg_pack_pkcs7(dst, NULL, 6), PKCS7_PACK_ERR);
	expect_untouched(0, SENTINEL_LEN);
}

ZTEST(pkcs7, test_msg_null_destination_replies_ca)
{
	static const uint8_t req[] = {0x4b, 0x00, 0x01, 0x00, 0x41};

	zassert_equal(parse_receiver_msg_pack_pkcs7(NULL, req, sizeof(req)), PKCS7_PACK_ERR);
}

/* DEFECT: as above, chunk 11 overruns the 0x800-byte buffer. */
ZTEST_EXPECT_FAIL(pkcs7, test_msg_chunk_overrunning_the_notification_buffer_is_rejected);
ZTEST(pkcs7, test_msg_chunk_overrunning_the_notification_buffer_is_rejected)
{
	uint8_t req[4 + APP_CHUNK_PAYLOAD];
	int got;

	memset(dst + NCS_BUF_SIZE, SENTINEL, GUARD_SIZE);
	req[0] = 0x4b;
	req[1] = 0x00;
	req[2] = 15;
	memset(req + 4, 'j', APP_CHUNK_PAYLOAD);
	for (int idx = 0; idx < 11; idx++) {
		req[3] = idx;
		got = parse_receiver_msg_pack_pkcs7(dst, req, sizeof(req));
		zassert_equal(got, PKCS7_PACK_MORE, "chunk %d (fits): 0x%02x", idx, got);
	}

	req[3] = 11;
	got = parse_receiver_msg_pack_pkcs7(dst, req, sizeof(req));

	expect_untouched(NCS_BUF_SIZE, GUARD_SIZE);
	zassert_equal(got, PKCS7_PACK_ERR, "chunk 11 (ends at %d): 0x%02x", 12 * 180, got);
}

/* DEFECT: as above, a resent chunk is appended again. */
ZTEST_EXPECT_FAIL(pkcs7, test_msg_duplicated_chunk_index_is_not_appended_twice);
ZTEST(pkcs7, test_msg_duplicated_chunk_index_is_not_appended_twice)
{
	uint8_t req[] = {0x4b, 0x00, 0x03, 0x01, 0x42};

	expect_msg("4b 00 03 00 41", PKCS7_PACK_MORE);
	expect_msg("4b 00 03 01 42", PKCS7_PACK_MORE);
	/* its reply is not asserted */
	parse_receiver_msg_pack_pkcs7(dst, req, sizeof(req));
	expect_msg("4b 00 03 02 43", PKCS7_PACK_DONE);
	expect_dst("41 42 43 7e");
}

/* ---- send ---- */

static void send_to_frames(uint8_t *buf, uint32_t len)
{
	g1_ble()->write(buf, (uint16_t)len);
}

/* data bytes cannot be mistaken for pad values */
static void send_a0_pattern(int len)
{
	uint8_t data[64];

	for (int i = 0; i < len; i++) {
		data[i] = (uint8_t)(0xa0 + i);
	}
	zassert_equal(subcontracing_send_data_pkcs7(data, len, 0x4b, send_to_frames), 0);
}

ZTEST(pkcs7, test_send_0_bytes_is_one_frame_of_full_padding)
{
	send_a0_pattern(0);

	expect_frame("4b 01 00 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11");
	expect_no_frame();
}

ZTEST(pkcs7, test_send_1_byte_is_one_frame_padded_with_16_bytes_of_0x10)
{
	send_a0_pattern(1);

	expect_frame("4b 01 00 a0 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10");
	expect_no_frame();
}

ZTEST(pkcs7, test_send_16_bytes_is_one_frame_padded_with_one_0x01)
{
	send_a0_pattern(16);

	expect_frame("4b 01 00 a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af 01");
	expect_no_frame();
}

ZTEST(pkcs7, test_send_17_bytes_is_one_data_frame_and_a_full_padding_frame)
{
	send_a0_pattern(17);

	expect_frame("4b 02 00 a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af b0");
	expect_frame("4b 02 01 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11");
	expect_no_frame();
}

ZTEST(pkcs7, test_send_18_bytes_is_two_frames_the_second_padded_with_16_bytes_of_0x10)
{
	send_a0_pattern(18);

	expect_frame("4b 02 00 a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af b0");
	expect_frame("4b 02 01 b1 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10");
	expect_no_frame();
}

/* 180 = 10 x 17 + 10 */
ZTEST(pkcs7, test_send_180_bytes_is_11_frames_the_last_padded_with_7_bytes_of_0x07)
{
	uint8_t data[180];

	for (int i = 0; i < 180; i++) {
		data[i] = (uint8_t)i;
	}
	zassert_equal(subcontracing_send_data_pkcs7(data, sizeof(data), 0x4b, send_to_frames), 0);

	expect_frame("4b 0b 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10");
	expect_frame("4b 0b 01 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f 20 21");
	expect_frame("4b 0b 02 22 23 24 25 26 27 28 29 2a 2b 2c 2d 2e 2f 30 31 32");
	expect_frame("4b 0b 03 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f 40 41 42 43");
	expect_frame("4b 0b 04 44 45 46 47 48 49 4a 4b 4c 4d 4e 4f 50 51 52 53 54");
	expect_frame("4b 0b 05 55 56 57 58 59 5a 5b 5c 5d 5e 5f 60 61 62 63 64 65");
	expect_frame("4b 0b 06 66 67 68 69 6a 6b 6c 6d 6e 6f 70 71 72 73 74 75 76");
	expect_frame("4b 0b 07 77 78 79 7a 7b 7c 7d 7e 7f 80 81 82 83 84 85 86 87");
	expect_frame("4b 0b 08 88 89 8a 8b 8c 8d 8e 8f 90 91 92 93 94 95 96 97 98");
	expect_frame("4b 0b 09 99 9a 9b 9c 9d 9e 9f a0 a1 a2 a3 a4 a5 a6 a7 a8 a9");
	expect_frame("4b 0b 0a aa ab ac ad ae af b0 b1 b2 b3 07 07 07 07 07 07 07");
	expect_no_frame();
}

ZTEST(pkcs7, test_sent_frames_reassemble_to_the_message_and_its_padding)
{
	uint8_t frame[20];
	int n;

	send_a0_pattern(18);

	n = g1_frame_pop(frame, sizeof(frame));
	zassert_equal(n, 20, "frame 0 length %d", n);
	zassert_equal(parse_receiver_pack_pkcs7(dst, frame, n), PKCS7_PACK_MORE);
	n = g1_frame_pop(frame, sizeof(frame));
	zassert_equal(n, 20, "frame 1 length %d", n);
	zassert_equal(parse_receiver_pack_pkcs7(dst, frame, n), PKCS7_PACK_DONE);

	expect_dst("a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af b0 b1"
		   " 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 7e");
}
