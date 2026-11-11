/* 0xF5 events: F5 <id> <payload>, 21 bytes. */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_misc.h"
#include "events_fakes.h"
#include "ble/ble_proto.h"
#include "dashboard/dashboard.h"

#define F5_FRAME_LEN 21

static void events_before(void *fixture)
{
	ARG_UNUSED(fixture);
	events_fakes_reset();
	memset(&_dashboard_prv_data, 0, sizeof(_dashboard_prv_data));
	notes_index = 0;
	stocks_index = 0;
	news_index = 0;
	calendar_index = 0;
}

ZTEST_SUITE(events, NULL, NULL, events_before, NULL, NULL);

static void report(uint8_t id, uint8_t arg)
{
	uint8_t evt[3] = {0xf5, id, arg};

	zassert_equal(deal_event_to_phone(g1_ble(), evt), 0, "event 0x%02x", id);
}

/* Byte 2 of the packet (CB) is not payload: sent as 00. */
ZTEST(events, test_events_without_payload_are_f5_id_and_zeros_21_bytes)
{
	static const uint8_t ids[] = {
		0x00, /* double tap */
		0x01, /* single tap */
		0x02, /* head up */
		0x03, /* head down */
		0x04, /* silent on */
		0x05, /* silent off */
		0x06, /* worn */
		0x07, /* not worn */
		0x08, /* lid open */
		0x0b, /* lid closed */
		0x0c, 0x0d, 0x10,
		0x11, /* BLE paired */
		0x13, 0x14, 0x15, 0x16, /* onboarding */
		0x17, /* long press held */
		0x18, /* long press released */
		0x19, 0x1a, 0x1b, 0x1c, 0x1d,
		0x1e, /* dashboard open */
		0x1f, /* dashboard close */
		0x20, /* translate double tap */
	};
	char hex[8];

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		report(ids[i], PKCS7_PACK_MORE);
		snprintf(hex, sizeof(hex), "f5 %02x", ids[i]);
		expect_frame_padded(hex, F5_FRAME_LEN);
	}
	expect_no_frame();
}

ZTEST(events, test_charging_event_0x09_carries_the_charging_flag)
{
	report(0x09, 1);
	expect_frame_padded("f5 09 01", F5_FRAME_LEN);
	report(0x09, 0);
	expect_frame_padded("f5 09 00", F5_FRAME_LEN);
	expect_no_frame();
}

ZTEST(events, test_battery_level_below_0x5d_is_reported_unchanged)
{
	static const uint8_t levels[] = {0x00, 0x01, 0x32, 0x56, 0x58, 0x5c};
	char hex[16];

	for (size_t i = 0; i < ARRAY_SIZE(levels); i++) {
		g1_state()->battery_left = levels[i];
		report(0x0a, 0);
		snprintf(hex, sizeof(hex), "f5 0a %02x", levels[i]);
		expect_frame_padded(hex, F5_FRAME_LEN);
	}
	expect_no_frame();
}

ZTEST(events, test_battery_event_ignores_the_charging_byte_of_the_packet)
{
	g1_state()->battery_left = 0x32;
	g1_ble()->charging = 1;

	report(0x0a, 1);

	expect_frame_padded("f5 0a 32", F5_FRAME_LEN);
}

/* 93..96 % are spread over 94..99 so 100 is reported only when the gauge reads 100. */
ZTEST(events, test_battery_level_0x5d_is_reported_as_0x5e_or_0x5f_with_the_soc_flag)
{
	g1_state()->battery_left = 0x5d;

	g1_state()->battery_soc_flag = 0;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 5e", F5_FRAME_LEN);

	g1_state()->battery_soc_flag = 1;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 5f", F5_FRAME_LEN);
}

ZTEST(events, test_battery_level_0x5e_is_reported_as_0x60_or_0x61_with_the_soc_flag)
{
	g1_state()->battery_left = 0x5e;

	g1_state()->battery_soc_flag = 0;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 60", F5_FRAME_LEN);

	g1_state()->battery_soc_flag = 1;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 61", F5_FRAME_LEN);
}

ZTEST(events, test_battery_level_0x5f_is_reported_as_0x62_whatever_the_soc_flag)
{
	g1_state()->battery_left = 0x5f;

	g1_state()->battery_soc_flag = 0;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 62", F5_FRAME_LEN);

	g1_state()->battery_soc_flag = 1;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 62", F5_FRAME_LEN);
}

ZTEST(events, test_battery_level_0x60_is_reported_as_0x63_whatever_the_soc_flag)
{
	g1_state()->battery_left = 0x60;

	g1_state()->battery_soc_flag = 0;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 63", F5_FRAME_LEN);

	g1_state()->battery_soc_flag = 1;
	report(0x0a, 0);
	expect_frame_padded("f5 0a 63", F5_FRAME_LEN);
}

ZTEST(events, test_battery_level_above_0x60_is_reported_as_0x64)
{
	static const uint8_t levels[] = {0x61, 0x63, 0x64};

	for (size_t i = 0; i < ARRAY_SIZE(levels); i++) {
		g1_state()->battery_left = levels[i];
		g1_state()->battery_soc_flag = i & 1;
		report(0x0a, 0);
		expect_frame_padded("f5 0a 64", F5_FRAME_LEN);
	}
	expect_no_frame();
}

ZTEST(events, test_case_charging_event_0x0e_carries_get_pt_level_flag)
{
	get_pt_level_flag_fake.return_val = 1;
	report(0x0e, PKCS7_PACK_MORE);
	expect_frame_padded("f5 0e 01", F5_FRAME_LEN);

	get_pt_level_flag_fake.return_val = 0;
	report(0x0e, PKCS7_PACK_MORE);
	expect_frame_padded("f5 0e 00", F5_FRAME_LEN);
	zassert_equal(get_pt_level_flag_fake.call_count, 2);
}

ZTEST(events, test_case_battery_event_0x0f_carries_get_pt_level)
{
	get_pt_level_fake.return_val = 0x28;
	report(0x0f, PKCS7_PACK_MORE);
	expect_frame_padded("f5 0f 28", F5_FRAME_LEN);

	get_pt_level_fake.return_val = 0x64;
	report(0x0f, PKCS7_PACK_MORE);
	expect_frame_padded("f5 0f 64", F5_FRAME_LEN);
}

ZTEST(events, test_brightness_event_0x12_carries_the_panel_brightness_level)
{
	g1_state()->panel.panel_brightness_level = 6;
	report(0x12, PKCS7_PACK_MORE);
	expect_frame_padded("f5 12 06", F5_FRAME_LEN);

	g1_state()->panel.panel_brightness_level = 0x0c;
	report(0x12, PKCS7_PACK_MORE);
	expect_frame_padded("f5 12 0c", F5_FRAME_LEN);
}

/* The master acks the last chunk of 0x0D / 0x0F / 0x4E only once the slave has it. */
ZTEST(events, test_deferred_ack_0xf0_is_0d_c9_seq_count_index_from_esb_down_id)
{
	g1_state()->esb_down_id = 0x01;
	g1_state()->esb_up_id = 0x77;
	deferred_text_ack_total = 1;
	deferred_text_ack_idx = 0;

	report(0xf0, PKCS7_PACK_MORE);

	expect_frame_padded("0d c9 01 01 00", F5_FRAME_LEN);
	expect_no_frame();
}

ZTEST(events, test_deferred_ack_0xf1_is_0f_c9_seq_count_index_from_esb_up_id)
{
	g1_state()->esb_up_id = 0x02;
	g1_state()->esb_down_id = 0x77;
	deferred_text_ack_total = 1;
	deferred_text_ack_idx = 0;

	report(0xf1, PKCS7_PACK_MORE);

	expect_frame_padded("0f c9 02 01 00", F5_FRAME_LEN);
	expect_no_frame();
}

ZTEST(events, test_deferred_ack_0xf2_is_4e_c9_seq_count_index_from_esb_up_id)
{
	g1_state()->esb_up_id = 0x2a;
	g1_state()->esb_down_id = 0x77;
	deferred_text_ack_total = 3;
	deferred_text_ack_idx = 2;

	report(0xf2, PKCS7_PACK_MORE);

	expect_frame_padded("4e c9 2a 03 02", F5_FRAME_LEN);
	expect_no_frame();
}

ZTEST(events, test_deferred_acks_clear_the_kept_count_and_index)
{
	static const uint8_t ids[] = {0xf0, 0xf1, 0xf2};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		deferred_text_ack_total = 5;
		deferred_text_ack_idx = 4;
		report(ids[i], PKCS7_PACK_MORE);
		zassert_equal(deferred_text_ack_total, 0, "after 0x%02x: total %u", ids[i],
			      deferred_text_ack_total);
		zassert_equal(deferred_text_ack_idx, 0, "after 0x%02x: idx %u", ids[i],
			      deferred_text_ack_idx);
	}

	g1_frames_clear();
	g1_state()->esb_down_id = 9;
	report(0xf0, PKCS7_PACK_MORE);
	expect_frame_padded("0d c9 09 00 00", F5_FRAME_LEN);
}

ZTEST(events, test_f5_packet_from_the_work_thread_is_sent_through_the_dispatcher)
{
	g1_state()->battery_left = 0x64;

	g1_send_hex("f5 06 cb");
	g1_send_hex("f5 0a 01");

	expect_frame_padded("f5 06", F5_FRAME_LEN);
	expect_frame_padded("f5 0a 64", F5_FRAME_LEN);
	expect_no_frame();
}
