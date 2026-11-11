/* '#' debug strings; unmatched ones reach the dispatcher as unknown PUT 0x23. */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <app_version.h>

#include "harness.h"
#include "dispatch.h"
#include "fakes_bt.h"
#include "fakes_misc.h"

#include "ble/ble_proto.h"

ZTEST(proto_dispatch, test_0x23_c_followed_by_more_text_is_not_an_unpair)
{
	g1_send_hex("23 63 31");

	zassert_equal(bt_unpair_fake.call_count, 0);
	expect_error_reply(0x23, "error put req.");
	expect_no_frame();
}

/*
 * DEFECT: the hook chunks with the glasses -> phone framing; the 0x4B handler rejects every chunk.
 */
ZTEST_EXPECT_FAIL(proto_dispatch, test_0x23_1_title_message_goes_through_the_0x4b_path);
ZTEST(proto_dispatch, test_0x23_1_title_message_goes_through_the_0x4b_path)
{
	const struct notif_msg *msg;
	int n;

	g1_send((const uint8_t *)"#1@title@msg", strlen("#1@title@msg"));

	n = g1_frame_count();
	zassert_true(n >= 2, "%d reply frames", n);
	for (int i = 0; i < n; i++) {
		size_t len;
		const uint8_t *f = g1_frame_peek(i, &len);
		uint8_t want = i == n - 1 ? 0xc9 : 0xcb;

		zassert_true(len >= 2 && f[0] == 0x4b, "frame %d: %02x", i, f[0]);
		zassert_equal(f[1], want, "chunk %d of %d: status %02x, want %02x", i + 1, n, f[1],
			      want);
	}
	g1_frames_clear();
	msg = (const struct notif_msg *)&g1_ble()->tx_buf[4];
	zassert_equal(strcmp(msg->title, "title"), 0, "title \"%s\"", msg->title);
	zassert_equal(strcmp(msg->message, "msg"), 0, "message \"%s\"", msg->message);
}

/* DEFECT (skipped): the unbounded scan for the second '@' reads the stack. */
ZTEST(proto_dispatch, test_0x23_1_without_second_at_stays_inside_the_packet)
{
	ztest_test_skip();

	g1_send((const uint8_t *)"#1@no second at", strlen("#1@no second at"));
	zassert_true(g1_frame_count() <= 32);
}

/* DEFECT (skipped): strncpy / strcpy overflow title[32] / message[128]. */
ZTEST(proto_dispatch, test_0x23_1_long_title_and_message_are_cut)
{
	char pkt[201];

	ztest_test_skip();

	memset(pkt, 'm', sizeof(pkt) - 1);
	memcpy(pkt, "#1@", 3);
	memset(pkt + 3, 't', 40);
	pkt[43] = '@';
	g1_send((const uint8_t *)pkt, sizeof(pkt) - 1);
	zassert_true(g1_frame_count() > 0);
}

/* DEFECT (skipped): unbounded '@' scan; memcpy of n bytes into cmd_type[4]. */
ZTEST(proto_dispatch, test_0x23_cmd_with_long_command_number_stays_inside_the_buffer)
{
	ztest_test_skip();

	g1_send((const uint8_t *)"#cmd@123456789@1", strlen("#cmd@123456789@1"));
}

ZTEST(proto_dispatch, test_0x23_t_replies_200_bytes_of_build_info)
{
	static uint8_t want[200];

	strcpy(net_build_time, "2025-10-22 14:21:16");
	JDB_PANEL_ID = 0x4010;
	memset(want, 0, sizeof(want));
	snprintf((char *)want, sizeof(want),
		 "net build time: 2025-10-22 14:21:16, app build time %s, ver %d.%d.%d, "
		 "JBD DeviceID 4010\n\r",
		 G1_TEST_APP_BUILD_TIME, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL);

	g1_send_vector("0x23_build_info");

	expect_frame_bytes(want, sizeof(want));
	expect_no_frame();
}

ZTEST(proto_dispatch, test_0x23_r_reboots_without_a_reply)
{
	g1_send_vector("0x23_reboot");

	zassert_equal(system_reboot_delayed_fake.call_count, 1);
	expect_no_frame();
}

/* 255 is the longest NUS packet (uint8_t len). */
ZTEST(proto_dispatch, test_0x23_unterminated_255_byte_string_is_an_unknown_put)
{
	uint8_t pkt[255];

	memset(pkt, 'x', sizeof(pkt));
	pkt[0] = '#';

	g1_send(pkt, sizeof(pkt));

	expect_error_reply(0x23, "error put req.");
	expect_no_frame();
}

ZTEST(proto_dispatch, test_0x23_t_in_an_unterminated_255_byte_string_replies_build_info)
{
	uint8_t pkt[255];
	size_t len;
	const uint8_t *f;

	memset(pkt, 'x', sizeof(pkt));
	pkt[0] = '#';
	pkt[1] = 't';

	g1_send(pkt, sizeof(pkt));

	f = g1_frame_peek(0, &len);
	zassert_not_null(f, "no reply");
	zassert_equal(len, 200);
	zassert_mem_equal(f, "net build time", strlen("net build time"));
	g1_frames_clear();
}

/* DEFECT (skipped): 256 bytes fill buf[256] and leave no NUL terminator. */
ZTEST(proto_dispatch, test_0x23_256_byte_packet_is_read_inside_the_buffer)
{
	uint8_t pkt[256];

	ztest_test_skip();

	memset(pkt, 'x', sizeof(pkt));
	pkt[0] = '#';
	g1_send(pkt, sizeof(pkt));
	expect_error_reply(0x23, "error put req.");
}

/* DEFECT (skipped): flash_ops.fn is never set and is called unchecked. */
ZTEST(proto_dispatch, test_0x23_j_does_not_call_an_unset_flash_callback)
{
	ztest_test_skip();

	g1_send_hex("23 6a 31");
	expect_frame_padded("69 6e 69 74 20 66 6c 61 73 68", 20);
}

/*
 * DEFECT: the hook strstr()s every packet, so "#2@" in an app name turns a whitelist chunk into a
 * head-up.
 */
ZTEST_EXPECT_FAIL(proto_dispatch, test_debug_marker_inside_a_packet_is_not_a_debug_command);
ZTEST(proto_dispatch, test_debug_marker_inside_a_packet_is_not_a_debug_command)
{
	uint8_t pkt[256];
	int n = g1_vector_load("0x04_whitelist_chunk2", pkt, sizeof(pkt));

	zassert_true(n > 24, "vector: %d", n);
	zassert_mem_equal(&pkt[16], "Discord", 7, "vector layout changed");
	memcpy(&pkt[16], "Di#2@1d", 7);

	g1_send_vector("0x04_whitelist_chunk1");
	expect_frame("04 cb");
	g1_send(pkt, n);

	zassert_equal(trigger_imu_state_change_fake.call_count, 0, "head-up simulated");
	expect_frame("04 c9");
	expect_no_frame();
}
