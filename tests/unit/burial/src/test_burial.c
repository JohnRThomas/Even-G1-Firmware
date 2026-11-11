/* A date before 2024-08 means the clock was never set (day index 7). */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_misc.h"
#include "utils/burial_point.h"

ZTEST_SUITE(burial, NULL, NULL, NULL, NULL, NULL);

/* UTC */
#define TS_2025_11_10_NOON 1762776000u
#define TS_2025_11_11_NOON 1762862400u
#define TS_2025_11_12_NOON 1762948800u
#define TS_2024_07_31_LAST 1722470399u
#define TS_2024_08_01_FIRST 1722470400u

static struct bp_context *bp(void)
{
	return &g1_state()->bp;
}

static void set_today(uint32_t ts)
{
	g1_state()->dashboard_ts->ts = ts;
}

static void stamp_slot(int i, uint16_t year, uint8_t month, uint8_t day)
{
	struct bp_day *days[3] = {bp()->wear_day, bp()->dashboard_day, bp()->notification_day};

	for (int k = 0; k < 3; k++) {
		days[k][i].year = year;
		days[k][i].month = month;
		days[k][i].day = day;
	}
}

static void assert_day(const struct bp_day *d, uint16_t year, uint8_t month, uint8_t day,
		       uint32_t sum_time, uint32_t count, const char *what)
{
	zassert_equal(d->year, year, "%s year %u", what, d->year);
	zassert_equal(d->month, month, "%s month %u", what, d->month);
	zassert_equal(d->day, day, "%s day %u", what, d->day);
	zassert_equal(d->sum_time, sum_time, "%s sum_time %u", what, d->sum_time);
	zassert_equal(d->count, count, "%s count %u", what, d->count);
}

static void today_in_slot_3(void)
{
	set_today(TS_2025_11_11_NOON);
	bp()->day_index = 3;
	stamp_slot(3, 2025, 11, 11);
}

/* ---- sessions ---- */

ZTEST(burial, test_dashboard_session_adds_its_whole_seconds_to_today_and_total)
{
	today_in_slot_3();

	enter_dashboard_burial_point();
	zassert_equal(g1_state()->bp_dashboard.active, 1);
	zassert_equal(g1_state()->bp_dashboard.day_index, 3);
	zassert_equal(bp()->dashboard_day[3].count, 1);
	zassert_equal(bp()->dashboard_count, 1);

	g1_clock_advance_ms(12000);
	exit_dashboard_burial_point();

	zassert_equal(g1_state()->bp_dashboard.active, 0);
	assert_day(&bp()->dashboard_day[3], 2025, 11, 11, 12, 1, "dashboard_day[3]");
	zassert_equal(bp()->dashboard_sum_time, 12);
	zassert_equal(bp()->dashboard_count, 1);
	assert_day(&bp()->wear_day[3], 2025, 11, 11, 0, 0, "wear_day[3]");
	assert_day(&bp()->notification_day[3], 2025, 11, 11, 0, 0, "notification_day[3]");
	zassert_equal(bp()->wear_sum_time, 0);
	zassert_equal(bp()->notification_sum_time, 0);
}

ZTEST(burial, test_dashboard_sessions_of_one_day_accumulate)
{
	today_in_slot_3();

	enter_dashboard_burial_point();
	g1_clock_advance_ms(12000);
	exit_dashboard_burial_point();
	g1_clock_advance_ms(60000); /* dashboard closed: not counted */
	enter_dashboard_burial_point();
	g1_clock_advance_ms(3000);
	exit_dashboard_burial_point();

	assert_day(&bp()->dashboard_day[3], 2025, 11, 11, 15, 2, "dashboard_day[3]");
	zassert_equal(bp()->dashboard_sum_time, 15);
	zassert_equal(bp()->dashboard_count, 2);
}

ZTEST(burial, test_notification_session_adds_its_whole_seconds_to_today_and_total)
{
	today_in_slot_3();

	enter_notification_burial_point();
	zassert_equal(g1_state()->bp_notification.active, 1);
	zassert_equal(g1_state()->bp_notification.day_index, 3);
	zassert_equal(bp()->notification_day[3].count, 1);
	zassert_equal(bp()->notification_count, 1);

	g1_clock_advance_ms(7000);
	exit_notification_burial_point();

	zassert_equal(g1_state()->bp_notification.active, 0);
	assert_day(&bp()->notification_day[3], 2025, 11, 11, 7, 1, "notification_day[3]");
	zassert_equal(bp()->notification_sum_time, 7);
	zassert_equal(bp()->notification_count, 1);
	assert_day(&bp()->dashboard_day[3], 2025, 11, 11, 0, 0, "dashboard_day[3]");
	zassert_equal(bp()->dashboard_sum_time, 0);
}

ZTEST(burial, test_wear_session_counts_entries_and_time_comes_from_the_minute_tick)
{
	today_in_slot_3();

	enter_wear_burial_point();
	zassert_equal(g1_state()->bp_wear.active, 1);
	zassert_equal(g1_state()->bp_wear.day_index, 3);
	zassert_equal(bp()->wear_day[3].count, 1);
	zassert_equal(bp()->wear_count, 1);

	g1_clock_advance_ms(60000);
	add_wear_burial_time(60000);
	g1_clock_advance_ms(60000);
	add_wear_burial_time(60000);
	g1_clock_advance_ms(30000);
	exit_wear_burial_point();

	/* exit does not add the 150 s again */
	zassert_equal(g1_state()->bp_wear.active, 0);
	assert_day(&bp()->wear_day[3], 2025, 11, 11, 120, 1, "wear_day[3]");
	zassert_equal(bp()->wear_sum_time, 120);
	zassert_equal(bp()->wear_count, 1);
}

ZTEST(burial, test_enter_while_a_session_is_active_neither_restarts_nor_recounts)
{
	today_in_slot_3();

	enter_dashboard_burial_point();
	g1_clock_advance_ms(5000);
	enter_dashboard_burial_point();
	g1_clock_advance_ms(5000);
	exit_dashboard_burial_point();

	/* timed from the first enter */
	assert_day(&bp()->dashboard_day[3], 2025, 11, 11, 10, 1, "dashboard_day[3]");
	zassert_equal(bp()->dashboard_count, 1);

	enter_wear_burial_point();
	enter_wear_burial_point();
	zassert_equal(bp()->wear_day[3].count, 1);
	zassert_equal(bp()->wear_count, 1);
}

ZTEST(burial, test_exit_without_enter_changes_nothing)
{
	struct bp_context before;

	today_in_slot_3();
	bp()->dashboard_day[3].sum_time = 40;
	bp()->dashboard_sum_time = 40;
	memcpy(&before, bp(), sizeof(before));

	g1_clock_advance_ms(10000);
	exit_dashboard_burial_point();
	exit_notification_burial_point();
	exit_wear_burial_point();

	zassert_mem_equal(bp(), &before, sizeof(before));
	zassert_equal(g1_state()->bp_dashboard.active, 0);
	zassert_equal(g1_state()->bp_notification.active, 0);
	zassert_equal(g1_state()->bp_wear.active, 0);
}

ZTEST(burial, test_factory_mode_records_nothing)
{
	today_in_slot_3();
	get_pt_test_mode_fake.return_val = 1;

	enter_dashboard_burial_point();
	enter_notification_burial_point();
	enter_wear_burial_point();
	g1_clock_advance_ms(10000);
	exit_dashboard_burial_point();
	exit_notification_burial_point();

	zassert_equal(g1_state()->bp_dashboard.active, 0);
	zassert_equal(g1_state()->bp_notification.active, 0);
	zassert_equal(g1_state()->bp_wear.active, 0);
	zassert_equal(bp()->dashboard_count + bp()->notification_count + bp()->wear_count, 0);
	zassert_equal(bp()->dashboard_sum_time + bp()->notification_sum_time, 0);
}

/* ---- day index ---- */

ZTEST(burial, test_date_before_august_2024_counts_as_not_set)
{
	uint8_t idx;

	zassert_equal(get_burial_point_day_index(), 7);

	set_today(TS_2024_07_31_LAST);
	zassert_equal(get_burial_point_day_index(), 7);
	enter_dashboard_burial_point();
	enter_notification_burial_point();
	enter_wear_burial_point();
	add_wear_burial_time(60000);
	zassert_equal(g1_state()->bp_dashboard.active, 0);
	zassert_equal(g1_state()->bp_notification.active, 0);
	zassert_equal(g1_state()->bp_wear.active, 0);
	zassert_equal(bp()->dashboard_count + bp()->notification_count + bp()->wear_count, 0);
	zassert_equal(bp()->wear_sum_time, 0);

	set_today(TS_2024_08_01_FIRST);
	idx = get_burial_point_day_index();
	zassert_true(idx < 7, "index %u", idx);
	assert_day(&bp()->wear_day[idx], 2024, 8, 1, 0, 0, "wear_day[idx]");
}

ZTEST(burial, test_new_day_takes_the_next_slot_stamped_and_zeroed)
{
	today_in_slot_3();
	stamp_slot(4, 2025, 11, 5);
	bp()->wear_day[4].sum_time = 3600;
	bp()->wear_day[4].count = 9;
	bp()->dashboard_day[4].sum_time = 300;
	bp()->dashboard_day[4].count = 8;
	bp()->notification_day[4].sum_time = 30;
	bp()->notification_day[4].count = 7;
	bp()->wear_sum_time = 5000;

	set_today(TS_2025_11_12_NOON);
	zassert_equal(get_burial_point_day_index(), 4);

	zassert_equal(bp()->day_index, 4);
	assert_day(&bp()->wear_day[4], 2025, 11, 12, 0, 0, "wear_day[4]");
	assert_day(&bp()->dashboard_day[4], 2025, 11, 12, 0, 0, "dashboard_day[4]");
	assert_day(&bp()->notification_day[4], 2025, 11, 12, 0, 0, "notification_day[4]");
	assert_day(&bp()->wear_day[3], 2025, 11, 11, 0, 0, "wear_day[3]");
	zassert_equal(bp()->wear_sum_time, 5000);
}

ZTEST(burial, test_day_index_wraps_from_slot_6_to_slot_0)
{
	set_today(TS_2025_11_11_NOON);
	bp()->day_index = 6;
	stamp_slot(6, 2025, 11, 11);
	stamp_slot(0, 2025, 11, 5);
	bp()->dashboard_day[0].count = 2;

	set_today(TS_2025_11_12_NOON);
	zassert_equal(get_burial_point_day_index(), 0);
	assert_day(&bp()->dashboard_day[0], 2025, 11, 12, 0, 0, "dashboard_day[0]");
	assert_day(&bp()->dashboard_day[6], 2025, 11, 11, 0, 0, "dashboard_day[6]");
}

/* DEFECT: a found slot falls through to the new-day code and is zeroed. */
ZTEST_EXPECT_FAIL(burial, test_a_date_already_in_the_ring_reuses_its_slot);
ZTEST(burial, test_a_date_already_in_the_ring_reuses_its_slot)
{
	today_in_slot_3();
	stamp_slot(2, 2025, 11, 10);
	bp()->dashboard_day[2].sum_time = 77;
	bp()->dashboard_day[2].count = 5;

	set_today(TS_2025_11_10_NOON);
	zassert_equal(get_burial_point_day_index(), 2);
	assert_day(&bp()->dashboard_day[2], 2025, 11, 10, 77, 5, "dashboard_day[2]");
	assert_day(&bp()->dashboard_day[3], 2025, 11, 11, 0, 0, "dashboard_day[3]");
}

ZTEST(burial, test_out_of_range_day_index_restarts_at_slot_0)
{
	set_today(TS_2025_11_11_NOON);
	bp()->day_index = 9;
	stamp_slot(0, 2025, 11, 11);
	bp()->wear_day[0].count = 4;

	zassert_equal(get_burial_point_day_index(), 0);
	zassert_equal(bp()->wear_day[0].count, 4);
}

/* ---- init ---- */

/* sessions open at init add no time on exit and count again on enter */
ZTEST(burial, test_record_info_init_ends_the_open_sessions_and_keeps_the_counters)
{
	today_in_slot_3();
	bp()->wear_sum_time = 7200;
	enter_dashboard_burial_point();
	enter_notification_burial_point();
	enter_wear_burial_point();
	g1_clock_advance_ms(10000);

	burial_point_record_info_init();
	exit_dashboard_burial_point();
	exit_notification_burial_point();
	enter_wear_burial_point();

	assert_day(&bp()->dashboard_day[3], 2025, 11, 11, 0, 1, "dashboard_day[3]");
	assert_day(&bp()->notification_day[3], 2025, 11, 11, 0, 1, "notification_day[3]");
	assert_day(&bp()->wear_day[3], 2025, 11, 11, 0, 2, "wear_day[3]");
	zassert_equal(bp()->dashboard_sum_time, 0);
	zassert_equal(bp()->notification_sum_time, 0);
	zassert_equal(bp()->wear_sum_time, 7200);
	zassert_equal(bp()->dashboard_count, 1);
	zassert_equal(bp()->notification_count, 1);
	zassert_equal(bp()->wear_count, 2);
}

/* ---- 0x3E reply ---- */

static void put_le16(uint8_t *b, size_t off, uint16_t v)
{
	b[off] = v & 0xff;
	b[off + 1] = v >> 8;
}

static void put_le32(uint8_t *b, size_t off, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		b[off + i] = (v >> (8 * i)) & 0xff;
	}
}

/* Offsets per glasses_state.h: little-endian, 12-byte days. */
ZTEST(burial, test_0x3e_replies_3e_c9_and_the_raw_counters_286_bytes)
{
	uint8_t want[286] = {0};
	uint8_t *r = &want[2];
	uint8_t got[300];
	int n;

	bp()->id = 0x11223344;
	bp()->wear_sum_time = 7200;
	bp()->wear_count = 3;
	bp()->dashboard_sum_time = 95;
	bp()->dashboard_count = 4;
	bp()->version = 1;
	bp()->day_index = 2;
	bp()->valid = 1;
	bp()->wear_day[2] = (struct bp_day){.year = 2025, .month = 11, .day = 11,
					    .sum_time = 7200, .count = 3};
	bp()->dashboard_day[6] = (struct bp_day){.year = 2025, .month = 11, .day = 7,
						 .sum_time = 95, .count = 4};
	bp()->notification_sum_time = 40;
	bp()->notification_count = 5;
	bp()->notification_day[6] = (struct bp_day){.year = 2025, .month = 11, .day = 7,
						    .sum_time = 40, .count = 0xa1b2c3d4};

	want[0] = 0x3e;
	want[1] = 0xc9;
	put_le32(r, 0x00, 0x11223344);
	put_le32(r, 0x04, 7200);
	put_le32(r, 0x08, 3);
	put_le32(r, 0x0c, 95);
	put_le32(r, 0x10, 4);
	r[0x14] = 1;
	r[0x15] = 2;
	r[0x16] = 1;
	put_le16(r, 0x30, 2025);        /* wear_day[2] */
	r[0x32] = 11;
	r[0x33] = 11;
	put_le32(r, 0x34, 7200);
	put_le32(r, 0x38, 3);
	put_le16(r, 0xb4, 2025);        /* dashboard_day[6] */
	r[0xb6] = 11;
	r[0xb7] = 7;
	put_le32(r, 0xb8, 95);
	put_le32(r, 0xbc, 4);
	put_le32(r, 0xc0, 40);
	put_le32(r, 0xc4, 5);
	put_le16(r, 0x110, 2025);       /* notification_day[6] */
	r[0x112] = 11;
	r[0x113] = 7;
	put_le32(r, 0x114, 40);
	put_le32(r, 0x118, 0xa1b2c3d4);

	g1_send_hex("3e");

	n = g1_frame_pop(got, sizeof(got));
	zassert_equal(n, 286, "reply of %d bytes", n);
	zassert_mem_equal(got, want, sizeof(want));
	expect_no_frame();
}
