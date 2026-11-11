/* 0x07 countdown: 07 <seconds u32 LE> <enable>. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_display.h"
#include "fakes_esb.h"
#include "ble/ble_proto.h"
#include "features.h"

static struct countdown_ts_context *cd(void)
{
	return g1_state()->countdown_ts;
}

ZTEST(features, test_0x07_start_sets_seconds_and_enabled)
{
	g1_send_hex("07 2c 01 00 00 01");

	zassert_equal(cd()->seconds, 300, "seconds %u", cd()->seconds);
	zassert_equal(cd()->enabled, 1);
}

ZTEST(features, test_0x07_seconds_are_a_little_endian_u32)
{
	g1_send_hex("07 0d 0c 0b 0a 01");

	zassert_equal(cd()->seconds, 0x0a0b0c0dU, "seconds 0x%08x", cd()->seconds);
}

ZTEST(features, test_0x07_replies_c9)
{
	g1_send_hex("07 2c 01 00 00 01");

	expect_frame_padded("07 c9", 20);
	expect_no_frame();
	feat_expect_queued_count(0);
}

/* task 2 = countdown */
ZTEST(features, test_0x07_start_opens_the_countdown_screen)
{
	g1_send_hex("07 2c 01 00 00 01");

	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal_ptr(update_persist_task_status_fake.arg0_val, g1_state());
	zassert_equal(update_persist_task_status_fake.arg1_val, 2);
	zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

ZTEST(features, test_0x07_tells_the_dashboard_thread)
{
	g1_send_hex("07 2c 01 00 00 01");

	zassert_equal(g1_state()->dashboard_cmd, 0x07);
	zassert_equal(k_sem_count_get(&g1_state()->dashboard_position_sem), 1);
}

/* reset makes countdown_tick() take a fresh one-second reference */
ZTEST(features, test_0x07_restart_replaces_the_running_countdown_and_sets_reset)
{
	cd()->seconds = 17;
	cd()->enabled = 1;
	cd()->reset = 0;

	g1_send_hex("07 78 00 00 00 01"); /* 120 s */

	zassert_equal(cd()->seconds, 120, "seconds %u", cd()->seconds);
	zassert_equal(cd()->enabled, 1);
	zassert_equal(cd()->reset, 1);
}

ZTEST(features, test_0x07_leaves_the_state_byte_alone)
{
	cd()->state = 1;

	g1_send_hex("07 2c 01 00 00 01");

	zassert_equal(cd()->state, 1);
}

ZTEST(features, test_0x07_stop_clears_enabled_without_opening_the_screen)
{
	cd()->seconds = 42;
	cd()->enabled = 1;

	g1_send_hex("07 00 00 00 00 00");

	zassert_equal(cd()->enabled, 0);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
	zassert_equal(g1_state()->dashboard_cmd, 0x07);
	expect_frame_padded("07 c9", 20);
	expect_no_frame();
}

/* #5@<min>@<enable> builds min * 60 + 3610 s */
ZTEST(features, test_0x07_debug_string_5_starts_a_countdown)
{
	g1_send((const uint8_t *)"#5@2@1", 6);

	zassert_equal(cd()->seconds, 3730, "seconds %u", cd()->seconds);
	zassert_equal(cd()->enabled, 1);
	zassert_equal(update_persist_task_status_fake.arg1_val, 2);
	expect_frame_padded("07 c9", 20);
	expect_no_frame();
}

/*
 * DEFECT: bytes 1..5 are read without a length check, so a short packet takes stale bytes from
 * small_buf.
 */
ZTEST_EXPECT_FAIL(features, test_0x07_packet_shorter_than_6_bytes_is_refused);
ZTEST(features, test_0x07_packet_shorter_than_6_bytes_is_refused)
{
	static const uint8_t earlier[] = {0x50, 0x06, 0x00, 0x00, 0x01, 0x01};

	memcpy(g1_ble()->small_buf, earlier, sizeof(earlier));

	g1_send_hex("07");

	zassert_equal(cd()->enabled, 0, "countdown enabled from stale bytes");
	zassert_equal(cd()->seconds, 0, "seconds %u from stale bytes", cd()->seconds);
	zassert_equal(cd()->reset, 0);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
}
