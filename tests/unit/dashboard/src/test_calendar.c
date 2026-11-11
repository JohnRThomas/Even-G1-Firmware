/*
 * Dashboard Set sub 3, calendar. Body: <dash mode> <pane 03> <schedule mode> [<count> <events>]; an
 * event is 01 title, 02 time, 03 location, each <type> <len> <bytes>. Stored strings must end with
 * a NUL inside their fields.
 */
#include "dash_test.h"
#include "fakes_display.h"

#define TITLE_SIZE 0x41
#define TIME_SIZE  0x21
#define LOC_SIZE   0x41

static uint8_t pkt[256];
static uint8_t body[1024];
static char str_a[0x60], str_b[0x60], str_c[0x60];

static uint8_t *put_field(uint8_t *p, uint8_t type, const char *s, size_t n)
{
	*p++ = type;
	*p++ = (uint8_t)n;
	memcpy(p, s, n);
	return p + n;
}

static uint8_t *put_event(uint8_t *p, const char *title, const char *time, const char *loc)
{
	p = put_field(p, 1, title, strlen(title));
	p = put_field(p, 2, time, strlen(time));
	return put_field(p, 3, loc, strlen(loc));
}

/* dual mode, calendar pane, schedule mode 3, count */
static uint8_t *put_list_header(uint8_t *p, uint8_t count)
{
	*p++ = 0x01;
	*p++ = 0x03;
	*p++ = 0x03;
	*p++ = count;
	return p;
}

static const char *repeat(char *buf, char c, size_t n)
{
	memset(buf, c, n);
	buf[n] = 0;
	return buf;
}

static size_t send_single(uint8_t seq, size_t n)
{
	size_t len = dash_chunk(pkt, seq, DASH_SUB_CALENDAR, 1, 1, body, n);

	zassert_true(len <= sizeof(pkt));
	g1_send(pkt, len);
	return len;
}

static void send_chunked(uint8_t seq, size_t n, size_t chunk)
{
	uint16_t total = (n + chunk - 1) / chunk;

	for (uint16_t i = 0; i < total; i++) {
		size_t part = MIN(chunk, n - i * chunk);
		size_t len = dash_chunk(pkt, seq + i, DASH_SUB_CALENDAR, total, i + 1,
					body + i * chunk, part);

		g1_send(pkt, len);
		expect_chunk_reply(pkt, 0x00);
	}
}

static void assert_event(int i, const char *title, const char *time, const char *loc)
{
	struct schedule_item *it = &schedule_items[i];

	zassert_equal(it->valid, 1, "event %d not valid", i);
	zassert_true(strcmp(it->title, title) == 0, "event %d title \"%.65s\", expected \"%s\"",
		     i, it->title, title);
	zassert_true(strcmp(it->time, time) == 0, "event %d time \"%.33s\", expected \"%s\"", i,
		     it->time, time);
	zassert_true(strcmp(it->location, loc) == 0,
		     "event %d location \"%.65s\", expected \"%s\"", i, it->location, loc);
}

static void assert_events_empty(int from)
{
	static const struct schedule_item zero;

	for (int i = from; i < 8; i++) {
		zassert_mem_equal(&schedule_items[i], &zero, sizeof(zero), "event %d not empty", i);
	}
}

static void store_one_old_event(void)
{
	uint8_t *p = put_list_header(body, 1);

	p = put_event(p, "Old", "1", "");
	send_single(0x01, p - body);
	expect_chunk_reply(pkt, 0x00);
	assert_event(0, "Old", "1", "");
}

/* ---- one packet ---- */

ZTEST(dashboard, test_0x06_calendar_one_packet_is_acked_with_its_nine_byte_header)
{
	g1_send_vector("0x06_calendar_two_events");

	expect_frame("06 53 00 0a 03 01 00 01 00 00");
	expect_no_frame();
}

ZTEST(dashboard, test_0x06_calendar_one_packet_two_events_fill_schedule_items)
{
	g1_send_vector("0x06_calendar_two_events");

	assert_event(0, "Tennis class w/ Garth", "Tmr 08:30-09:30", "");
	assert_event(1, "All day", "Tmr 00:00-12:00", "");
	assert_events_empty(2);
	zassert_equal(dts()->sched_num, 2);
	zassert_equal(dts()->sched_mode, 3);
	zassert_equal(dts()->disp_mode, 1);
	zassert_equal(dts()->area, 3);
}

ZTEST(dashboard, test_0x06_calendar_without_event_list_stores_the_mode_and_is_acked)
{
	g1_send_vector("0x06_calendar_empty");

	expect_frame("06 0c 00 09 03 01 00 01 00 00");
	zassert_equal(dts()->sched_mode, 2);
	zassert_equal(dts()->disp_mode, 1);
	zassert_equal(dts()->area, 3);
}

ZTEST(dashboard, test_0x06_calendar_event_with_three_empty_strings_is_stored_as_valid_and_empty)
{
	uint8_t *p = put_list_header(body, 2);

	p = put_event(p, "", "", "");
	p = put_event(p, "Next", "10:00", "Room 2");
	send_single(0x10, p - body);

	expect_chunk_reply(pkt, 0x00);
	assert_event(0, "", "", "");
	assert_event(1, "Next", "10:00", "Room 2");
	zassert_equal(dts()->sched_num, 2);
}

ZTEST(dashboard, test_0x06_calendar_eight_events_fill_every_slot)
{
	static const char *const titles[8] = {"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"};
	uint8_t *p = put_list_header(body, 8);

	for (int i = 0; i < 8; i++) {
		p = put_event(p, titles[i], "t", "l");
	}
	send_single(0x11, p - body);

	expect_chunk_reply(pkt, 0x00);
	for (int i = 0; i < 8; i++) {
		assert_event(i, titles[i], "t", "l");
	}
	zassert_equal(dts()->sched_num, 8);
}

ZTEST(dashboard, test_0x06_calendar_ninth_event_keeps_the_stored_schedule)
{
	uint8_t *p;

	store_one_old_event();
	p = put_list_header(body, 9);
	for (int i = 0; i < 9; i++) {
		p = put_event(p, "N", "t", "");
	}
	send_single(0x12, p - body);
	g1_frames_clear();

	assert_event(0, "Old", "1", "");
	assert_events_empty(1);
	zassert_equal(dts()->sched_num, 1);
}

/* Defect: an oversized list gets no reply, though the app retries unanswered packets. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_calendar_ninth_event_gets_an_error_reply);
ZTEST(dashboard, test_0x06_calendar_ninth_event_gets_an_error_reply)
{
	uint8_t *p = put_list_header(body, 9);

	for (int i = 0; i < 9; i++) {
		p = put_event(p, "N", "t", "");
	}
	send_single(0x12, p - body);

	zassert_equal(g1_frame_count(), 1, "%d replies", g1_frame_count());
	expect_chunk_reply(pkt, 0x01);
}

ZTEST(dashboard, test_0x06_calendar_strings_one_below_the_field_sizes_are_stored_whole)
{
	const char *t = repeat(str_a, 'T', TITLE_SIZE - 1);
	const char *h = repeat(str_b, 'H', TIME_SIZE - 1);
	const char *l = repeat(str_c, 'L', LOC_SIZE - 1);
	uint8_t *p = put_list_header(body, 2);

	p = put_event(p, t, h, l);
	p = put_event(p, "Next", "9", "");
	send_single(0x13, p - body);

	expect_chunk_reply(pkt, 0x00);
	assert_event(0, t, h, l);
	assert_event(1, "Next", "9", "");
}

ZTEST(dashboard, test_0x06_calendar_strings_of_the_field_sizes_are_cut_and_terminated)
{
	uint8_t *p = put_list_header(body, 2);

	p = put_event(p, repeat(str_a, 'T', TITLE_SIZE), repeat(str_b, 'H', TIME_SIZE),
		      repeat(str_c, 'L', LOC_SIZE));
	p = put_event(p, "Next", "9", "");
	send_single(0x14, p - body);

	expect_chunk_reply(pkt, 0x00);
	assert_event(0, repeat(str_a, 'T', TITLE_SIZE - 1), repeat(str_b, 'H', TIME_SIZE - 1),
		     repeat(str_c, 'L', LOC_SIZE - 1));
	assert_event(1, "Next", "9", "");
}

ZTEST(dashboard, test_0x06_calendar_new_list_replaces_every_previous_event)
{
	uint8_t *p;

	g1_send_vector("0x06_calendar_two_events");
	p = put_list_header(body, 1);
	p = put_event(p, "Only", "11:00", "");
	send_single(0x16, p - body);

	assert_event(0, "Only", "11:00", "");
	assert_events_empty(1);
	zassert_equal(dts()->sched_num, 1);
}

ZTEST(dashboard, test_0x06_calendar_changed_event_count_resets_the_page_index)
{
	g1_send_vector("0x06_calendar_two_events");
	zassert_equal(cleanCalenadrIndex_fake.call_count, 1);

	g1_send_vector("0x06_calendar_two_events");
	zassert_equal(cleanCalenadrIndex_fake.call_count, 1, "same count reset the page");
}

ZTEST(dashboard, test_0x06_calendar_count_and_index_are_16_bit)
{
	int n = g1_vector_load("0x06_calendar_two_events", pkt, sizeof(pkt));

	zassert_equal(n, 83);
	pkt[6] = 0x01; /* count 0x0101 */
	g1_send(pkt, n);

	expect_frame("06 53 00 0a 03 01 01 01 00 00");
	assert_events_empty(0);
	zassert_equal(dts()->sched_num, 0);
}

/* ---- chunks ---- */

ZTEST(dashboard, test_0x06_calendar_two_chunks_are_acked_chunk_by_chunk)
{
	g1_send_vector("0x06_calendar_chunk1");
	expect_frame("06 bd 00 3a 03 02 00 01 00 00");
	expect_no_frame();

	g1_send_vector("0x06_calendar_chunk2");
	expect_frame("06 6a 00 3b 03 02 00 02 00 00");
	expect_no_frame();
}

ZTEST(dashboard, test_0x06_calendar_two_chunks_store_the_six_events_of_the_app_log)
{
	g1_send_vector("0x06_calendar_chunk1");
	assert_events_empty(0); /* nothing parsed before the last chunk */
	g1_send_vector("0x06_calendar_chunk2");

	assert_event(0, "Tonight", "09:00-10:00", "");
	assert_event(1, "After", "10:00-11:00", "My butt");
	assert_event(2, "Tennis class w/ Garth", "Tmr 08:30-09:30", "");
	assert_event(3, "Another event", "Tmr 12:30-01:30", "A long long long long location");
	assert_event(4, "This is a long title that is super super long", "Tmr 08:30-09:30", "");
	assert_event(5, "More events how many pages?", "Tmr 10:00-11:00", "");
	assert_events_empty(6);
	zassert_equal(dts()->sched_num, 6);
	zassert_equal(dts()->sched_mode, 3);
	zassert_equal(dts()->disp_mode, 1);
	zassert_equal(dts()->area, 3);
}

ZTEST(dashboard, test_0x06_calendar_chunked_strings_one_below_the_field_sizes_are_stored_whole)
{
	const char *t = repeat(str_a, 'T', TITLE_SIZE - 1);
	const char *h = repeat(str_b, 'H', TIME_SIZE - 1);
	const char *l = repeat(str_c, 'L', LOC_SIZE - 1);
	uint8_t *p = put_list_header(body, 2);

	p = put_event(p, t, h, l);
	p = put_event(p, "Next", "9", "");
	send_chunked(0x20, p - body, 100);

	assert_event(0, t, h, l);
	assert_event(1, "Next", "9", "");
}

/* Defect: the chunked path copies up to the full field size, leaving no NUL. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_calendar_chunked_strings_of_the_field_sizes_are_cut_and_terminated);
ZTEST(dashboard, test_0x06_calendar_chunked_strings_of_the_field_sizes_are_cut_and_terminated)
{
	uint8_t *p = put_list_header(body, 2);
	struct schedule_item *it = &schedule_items[0];

	p = put_event(p, repeat(str_a, 'T', TITLE_SIZE), repeat(str_b, 'H', TIME_SIZE),
		      repeat(str_c, 'L', LOC_SIZE));
	p = put_event(p, "Next", "9", "");
	send_chunked(0x24, p - body, 100);

	zassert_equal(it->title[TITLE_SIZE - 1], 0, "title not terminated");
	zassert_equal(it->time[TIME_SIZE - 1], 0, "time not terminated");
	zassert_equal(it->location[LOC_SIZE - 1], 0, "location not terminated");
	assert_event(0, repeat(str_a, 'T', TITLE_SIZE - 1), repeat(str_b, 'H', TIME_SIZE - 1),
		     repeat(str_c, 'L', LOC_SIZE - 1));
	assert_event(1, "Next", "9", "");
}

ZTEST(dashboard, test_0x06_calendar_chunk_2_without_chunk_1_is_refused_with_status_1)
{
	store_one_old_event();

	g1_send_vector("0x06_calendar_chunk2");

	expect_frame("06 6a 00 3b 03 02 00 02 00 01");
	expect_no_frame();
	assert_event(0, "Old", "1", "");
	assert_events_empty(1);
}

ZTEST(dashboard, test_0x06_calendar_skipped_chunk_is_refused_and_nothing_is_parsed)
{
	uint8_t *p;
	size_t n, len;

	store_one_old_event();
	p = put_list_header(body, 3);
	for (int i = 0; i < 3; i++) {
		p = put_event(p, "Event", "12:00-13:00", "Somewhere");
	}
	n = p - body;

	len = dash_chunk(pkt, 0x30, DASH_SUB_CALENDAR, 3, 1, body, 30);
	g1_send(pkt, len);
	expect_chunk_reply(pkt, 0x00);
	len = dash_chunk(pkt, 0x32, DASH_SUB_CALENDAR, 3, 3, body + 60, n - 60);
	g1_send(pkt, len);
	expect_chunk_reply(pkt, 0x01);

	assert_event(0, "Old", "1", "");
	assert_events_empty(1);
}
