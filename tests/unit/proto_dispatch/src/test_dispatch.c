/*
 * Phone packet routing: PUT 0x01..0x27, GET 0x29..0x45, POST 0x47..0x56, report 0xF1..0xF6, else
 * "NN".
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "dispatch.h"

ZTEST_SUITE(proto_dispatch, NULL, NULL, NULL, NULL, NULL);

static void send_byte(uint8_t id)
{
	g1_send(&id, 1);
}

/* 0x11 has no case in ble_process_put_req. */
ZTEST(proto_dispatch, test_unknown_ids_inside_the_put_range_get_the_put_error)
{
	static const uint8_t ids[] = {0x0c, 0x11, 0x12, 0x13, 0x19, 0x1a, 0x1b, 0x1d, 0x21};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		send_byte(ids[i]);
		expect_error_reply(ids[i], "error put req.");
	}
	expect_no_frame();
}

ZTEST(proto_dispatch, test_get_range_ends_at_0x45)
{
	send_byte(0x45);

	expect_error_reply(0x45, "error get req.");
	expect_no_frame();
}

ZTEST(proto_dispatch, test_unknown_ids_inside_the_get_range_get_the_get_error)
{
	static const uint8_t ids[] = {0x2f, 0x30, 0x31, 0x40, 0x41, 0x42, 0x43, 0x44};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		send_byte(ids[i]);
		expect_error_reply(ids[i], "error get req.");
	}
	expect_no_frame();
}

ZTEST(proto_dispatch, test_unknown_id_inside_the_post_range_gets_the_post_error)
{
	send_byte(0x48);

	expect_error_reply(0x48, "error post req.");
	expect_no_frame();
}

/* Byte 0 is covered by the EXPECT_FAIL test below. */
ZTEST(proto_dispatch, test_report_range_ends_at_0xf6)
{
	uint8_t want[20] = {0xf6, 0xca, 'e', 'r', 'r', 'o', 'r', ' ', 'p', 'o', 's', 't',
			    ' ', 'r', 'e', 'q', '.'};
	size_t len;
	const uint8_t *f;

	send_byte(0xf6);

	f = g1_frame_peek(0, &len);
	zassert_not_null(f, "no reply");
	zassert_equal(len, 20, "reply of %u bytes", (unsigned int)len);
	zassert_mem_equal(f + 1, want + 1, 19);
	g1_frames_clear();
}

/* DEFECT: ble_process_report_response() never sets resp[0], so these start with 00. */
ZTEST_EXPECT_FAIL(proto_dispatch, test_unknown_report_ids_echo_the_command_in_the_error_reply);
ZTEST(proto_dispatch, test_unknown_report_ids_echo_the_command_in_the_error_reply)
{
	static const uint8_t ids[] = {0xf2, 0xf3, 0xf6};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		send_byte(ids[i]);
		expect_error_reply(ids[i], "error post req.");
	}
}

ZTEST(proto_dispatch, test_ids_outside_every_range_reply_nn)
{
	int id;

	for (id = 0x00; id <= 0xff; id++) {
		bool unhandled = id == 0x00 || id == 0x28 || id == 0x46 ||
				 (id >= 0x57 && id <= 0xf0) || id >= 0xf7;
		size_t len;
		const uint8_t *f;

		if (!unhandled) {
			continue;
		}
		send_byte(id);
		f = g1_frame_peek(0, &len);
		zassert_not_null(f, "id 0x%02x: no reply", id);
		zassert_true(len == 3 && f[0] == 'N' && f[1] == 'N' && f[2] == 0,
			     "id 0x%02x: reply of %u bytes %02x %02x ...", id, (unsigned int)len,
			     f[0], len > 1 ? f[1] : 0);
		zassert_equal(g1_frame_count(), 1, "id 0x%02x: %d frames", id, g1_frame_count());
		g1_frames_clear();
	}
}

ZTEST(proto_dispatch, test_packet_of_255_bytes_is_dispatched_and_answered)
{
	uint8_t pkt[255] = {0x3b};

	g1_send(pkt, sizeof(pkt));

	expect_frame_padded("3b c9 00 00", 20);
}

ZTEST(proto_dispatch, test_replies_are_20_byte_frames_padded_with_zeros)
{
	g1_state()->check_mode = 1;

	/* MACs unset (0xff) */
	g1_send_vector("0x2d_mac_get");
	expect_frame_padded("2d 67 ff ff ff ff ff ff ff ff ff ff ff ff", 20);

	g1_send_vector("0x3a_wear_detection_get");
	expect_frame_padded("3a c9 01", 20);

	g1_send_vector("0x0e_mic_off"); /* PUT */
	expect_frame_padded("0e c9 00", 20);

	g1_send_vector("0x4d_mtu_251"); /* POST */
	expect_frame_padded("4d c9", 20);
	expect_no_frame();
}

/*
 * DEFECT (skipped, crashes): length 0 dispatches the stale data[0] with hdr.len 0, and PUT
 * memcpy()s (size_t)-1 bytes.
 */
ZTEST(proto_dispatch, test_zero_length_packet_is_ignored)
{
	uint8_t none[1] = {0};

	ztest_test_skip();

	g1_send_vector("0x01_brightness");
	g1_frames_clear();
	g1_send(none, 0);
	expect_no_frame();
}

/* DEFECT (skipped): switch_to_dfu_cb is never set and is called unchecked. */
ZTEST(proto_dispatch, test_0x4a_dfu_bit_without_a_dfu_callback_is_safe)
{
	ztest_test_skip();

	g1_send_hex("4a 01");
	expect_frame_padded("4a c9", 20);
}
