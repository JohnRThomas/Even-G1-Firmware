/* 0xF4 debug channel: F4 <text> 00, queue of 20 lines of 200 bytes. */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "ble/ble_proto.h"

extern int debug_pending;

static void expect_debug_frame(const char *text)
{
	static char hex[3 * (DEBUG_MSG_SIZE + 2) + 1];
	size_t o = 0;

	o += snprintf(hex + o, sizeof(hex) - o, "f4");
	for (const char *p = text; *p; p++) {
		o += snprintf(hex + o, sizeof(hex) - o, " %02x", (uint8_t)*p);
	}
	snprintf(hex + o, sizeof(hex) - o, " 00");
	expect_frame(hex);
}

ZTEST(events, test_debug_line_is_sent_as_f4_text_and_nul)
{
	ble_printk("battery %d%%\n", 87);

	zassert_equal(send_debug_msg(g1_ble()), 0);

	expect_frame("f4 62 61 74 74 65 72 79 20 38 37 25 0a 00"); /* "battery 87%\n" */
	expect_no_frame();
}

ZTEST(events, test_send_debug_msg_with_an_empty_queue_sends_nothing)
{
	zassert_equal(send_debug_msg(g1_ble()), 0);
	expect_no_frame();
}

ZTEST(events, test_debug_queue_holds_20_lines_and_the_21st_drops_the_oldest)
{
	char line[16];

	for (int i = 0; i < DEBUG_MSGQ_MAX + 1; i++) {
		ble_printk("line %d\n", i);
	}

	send_debug_msg(g1_ble());

	for (int i = 1; i < DEBUG_MSGQ_MAX + 1; i++) {
		snprintf(line, sizeof(line), "line %d\n", i);
		expect_debug_frame(line);
	}
	expect_no_frame();
}

ZTEST(events, test_debug_queue_full_of_20_lines_drops_nothing)
{
	char line[16];

	for (int i = 0; i < DEBUG_MSGQ_MAX; i++) {
		ble_printk("line %d\n", i);
	}

	send_debug_msg(g1_ble());

	for (int i = 0; i < DEBUG_MSGQ_MAX; i++) {
		snprintf(line, sizeof(line), "line %d\n", i);
		expect_debug_frame(line);
	}
	expect_no_frame();
}

ZTEST(events, test_debug_line_longer_than_199_characters_is_cut_to_199_and_nul)
{
	char text[251];
	uint8_t got[260];
	int n;

	memset(text, 'x', 250);
	text[250] = '\0';
	ble_printk("%s", text);

	send_debug_msg(g1_ble());

	n = g1_frame_pop(got, sizeof(got));
	zassert_equal(n, 1 + 199 + 1, "frame of %d bytes", n);
	zassert_equal(got[0], 0xf4);
	for (int i = 1; i <= 199; i++) {
		zassert_equal(got[i], 'x', "byte %d = 0x%02x", i, got[i]);
	}
	zassert_equal(got[200], 0x00);
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

	expect_frame("f4 68 65 6c 6c 6f 0a 00");
	expect_no_frame();
}

ZTEST(events, test_firmware_log_line_goes_to_the_phone_when_debug_logging_is_on)
{
	BLE_DEBUG = 1;

	g1_send_hex("60");
	expect_frame("4e 4e 00");
	expect_no_frame();

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
