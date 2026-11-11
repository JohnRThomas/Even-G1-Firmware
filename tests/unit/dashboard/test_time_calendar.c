/*
 * Dashboard Set sub 1 (time and weather) and sub 3 (calendar). The sub 1 stamps are local time
 * encoded as UTC. Calendar body: <dash mode> <pane 03> <schedule mode> [<count> <events>]; an event
 * is 01 title, 02 time, 03 location, each <type> <len> <bytes>. Stored strings must end with a NUL
 * inside their fields.
 */
#include "dash_test.h"
#include "utils/common_utils.h"

#define TITLE_SIZE 0x41
#define TIME_SIZE  0x21
#define LOC_SIZE   0x41

ZTEST_SUITE(dashboard, NULL, NULL, NULL, NULL, NULL);

static uint8_t pkt[256];
static uint8_t body[1024];
static char title_text[0x60], time_text[0x60], loc_text[0x60];

/* ---- sub 1: time and weather ---- */

ZTEST(dashboard, test_0x06_time_weather_replies_the_6_byte_echo_with_status_0)
{
  g1_send_vector("0x06_time_weather_sunny");

  expect_only_frame("06 16 00 09 01 00");
}

ZTEST(dashboard, test_0x06_time_weather_stores_icon_temperature_both_stamps_and_formats)
{
  g1_send_vector("0x06_time_weather_sunny");

  zassert_equal(dts()->ts, 1762603160u);
  zassert_equal(dts()->ms_ts, 1762603160491ull);
  zassert_equal(dts()->weather_icon, 0x10);
  zassert_equal(dts()->temperature, 0x14);
  zassert_equal(dts()->temp_unit, 1);
  zassert_equal(dts()->time_disp_mode, 1);
  zassert_equal(dts()->date_format, 0);
}

ZTEST(dashboard, test_0x06_time_without_weather_stores_ff_ff_and_the_stamps)
{
  g1_send_vector("0x06_time_weather");

  expect_frame("06 16 00 09 01 00");
  zassert_equal(dts()->weather_icon, 0xff);
  zassert_equal(dts()->temperature, 0xff);
  zassert_equal(dts()->ts, 1762981198u);
  zassert_equal(dts()->ms_ts, 1762981198349ull);
}

ZTEST(dashboard, test_0x06_time_sets_the_wall_clock_from_the_millisecond_stamp)
{
  int64_t ms;

  g1_clock_advance_ms(4000); /* catches a boot-relative clock */
  g1_send_vector("0x06_time_weather_sunny");

  get_wall_clock_ms(&ms);
  zassert_equal(ms, 1762603160491ll, "wall clock %lld", ms);
  g1_clock_advance_ms(1500);
  get_wall_clock_ms(&ms);
  zassert_equal(ms, 1762603160491ll + 1500, "wall clock %lld", ms);
}

/* 2.5 s after arrival: 2 whole seconds, not the time since boot */
ZTEST(dashboard, test_0x06_time_restarts_the_dashboard_clock_reference)
{
  g1_clock_advance_ms(7000);
  g1_send_vector("0x06_time_weather_sunny");
  g1_clock_advance_ms(2500);

  accumulate_elapsed_seconds((uint32_t *)(void *)dts(), 0, 0); /* ts is at offset 0 */
  zassert_equal(dts()->ts, 1762603160u + 2, "ts %u", dts()->ts);
}

ZTEST(dashboard, test_0x06_time_echoes_the_sequence_byte)
{
  g1_send_vector_with("0x06_time_weather_sunny", 3, 0xa5);

  expect_frame("06 16 00 a5 01 00");
}

ZTEST(dashboard, test_0x06_time_length_field_mismatch_replies_status_1_and_stores_nothing)
{
  int64_t ms;

  g1_send_vector_with("0x06_time_weather_sunny", 1, 0x17);

  expect_only_frame_padded("06 17 00 09 01 01", 20);
  zassert_equal(dts()->ts, 0);
  zassert_equal(dts()->ms_ts, 0);
  zassert_equal(dts()->weather_icon, 0);
  get_wall_clock_ms(&ms);
  zassert_equal(ms, 0, "wall clock set to %lld", ms);
}

/* byte 21 is date_format */
ZTEST(dashboard, test_0x06_time_changed_date_format_is_stored_and_committed)
{
  struct GlassesState *gs = g1_state();

  gs->dashboard_cmd = 0x55;
  g1_send_vector_with("0x06_time_weather_sunny", 21, 1);

  zassert_equal(dts()->date_format, 1);
  zassert_equal(gs->dashboard_cmd, 0);
  zassert_true(g1_dashboard_notified());
}

ZTEST(dashboard, test_0x06_time_unchanged_date_format_requests_no_commit)
{
  g1_send_vector("0x06_time_weather_sunny");

  zassert_equal(dts()->date_format, 0);
  zassert_false(g1_dashboard_notified());
}

ZTEST(dashboard, test_0x06_time_21_byte_form_keeps_the_stored_date_format)
{
  int len = g1_vector("0x06_time_weather_sunny", pkt, sizeof(pkt));

  dts()->date_format = 2; /* the rx buffer byte behind it would read 0 */
  pkt[1] = 0x15;
  g1_send(pkt, len - 1);

  expect_frame("06 15 00 09 01 00");
  zassert_equal(dts()->date_format, 2);
  zassert_equal(dts()->ts, 1762603160u);
}

ZTEST(dashboard, test_0x06_time_is_saved_to_retained_memory)
{
  g1_send_vector("0x06_time_weather_sunny");

  zassert_equal(save_dashboard_ts_to_ncs_mem_fake.call_count, 1);
  zassert_equal(set_ncs_mem_initialized_fake.call_count, 1);
}

/* ---- sub 3: calendar ---- */

static uint8_t *put_event(uint8_t *pos, const char *title, const char *time, const char *loc)
{
  pos = put_tlv8(pos, 1, title, strlen(title));
  pos = put_tlv8(pos, 2, time, strlen(time));
  return put_tlv8(pos, 3, loc, strlen(loc));
}

/* body: dual mode, calendar pane, schedule mode 3, count */
static uint8_t *put_list_header(uint8_t count)
{
  const uint8_t header[] = {0x01, 0x03, 0x03, count};

  memcpy(body, header, sizeof(header));
  return body + sizeof(header);
}

/* count events with the same strings; returns the body length */
static size_t put_same_events(uint8_t count, const char *title, const char *time, const char *loc)
{
  uint8_t *pos = put_list_header(count);

  for (int event = 0; event < count; event++) {
    pos = put_event(pos, title, time, loc);
  }
  return pos - body;
}

/* event 0 has strings of the field sizes + extra bytes, event 1 is short */
static size_t put_long_strings(int extra)
{
  uint8_t *pos = put_list_header(2);

  pos = put_event(pos, repeat(title_text, 'T', TITLE_SIZE + extra),
      repeat(time_text, 'H', TIME_SIZE + extra),
      repeat(loc_text, 'L', LOC_SIZE + extra));
  pos = put_event(pos, "Next", "9", "");
  return pos - body;
}

static void send_calendar_single(uint8_t seq, size_t len)
{
  size_t pkt_len = g1_put_chunk(pkt, 0x06, seq, DASH_SUB_CALENDAR, 1, 1, body, len);

  zassert_true(pkt_len <= sizeof(pkt));
  g1_send(pkt, pkt_len);
}

/* 100-byte chunks, each acked with 00 */
static void send_calendar_chunked(uint8_t seq, size_t len)
{
  uint16_t total = DIV_ROUND_UP(len, 100);

  for (uint16_t index = 0; index < total; index++) {
    g1_send(pkt,
      g1_put_chunk(pkt, 0x06, seq + index, DASH_SUB_CALENDAR, total, index + 1,
             body + index * 100, MIN(100, len - index * 100)));
    expect_chunk_reply(pkt, 0x00);
  }
}

static void assert_event(int slot, const char *title, const char *time, const char *loc)
{
  struct schedule_item *item = &schedule_items[slot];

  zassert_equal(item->valid, 1, "event %d not valid", slot);
  zassert_true(strcmp(item->title, title) == 0, "event %d title \"%.65s\", expected \"%s\"",
         slot, item->title, title);
  zassert_true(strcmp(item->time, time) == 0, "event %d time \"%.33s\", expected \"%s\"",
         slot, item->time, time);
  zassert_true(strcmp(item->location, loc) == 0,
         "event %d location \"%.65s\", expected \"%s\"", slot, item->location, loc);
}

/* the events of put_long_strings() with the strings one below the field sizes */
static void assert_longest_strings(void)
{
  assert_event(0, repeat(title_text, 'T', TITLE_SIZE - 1),
         repeat(time_text, 'H', TIME_SIZE - 1), repeat(loc_text, 'L', LOC_SIZE - 1));
  assert_event(1, "Next", "9", "");
}

static void assert_events_empty(int from)
{
  static const struct schedule_item zero;

  for (int slot = from; slot < 8; slot++) {
    zassert_mem_equal(&schedule_items[slot], &zero, sizeof(zero), "event %d not empty",
          slot);
  }
}

static void store_one_old_event(void)
{
  send_calendar_single(0x01, put_same_events(1, "Old", "1", ""));
  expect_chunk_reply(pkt, 0x00);
  assert_event(0, "Old", "1", "");
}

ZTEST(dashboard, test_0x06_calendar_one_packet_is_acked_with_its_nine_byte_header)
{
  g1_send_vector("0x06_calendar_two_events");

  expect_only_frame("06 53 00 0a 03 01 00 01 00 00");
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
  uint8_t *pos = put_list_header(2);

  pos = put_event(pos, "", "", "");
  pos = put_event(pos, "Next", "10:00", "Room 2");
  send_calendar_single(0x10, pos - body);

  expect_chunk_reply(pkt, 0x00);
  assert_event(0, "", "", "");
  assert_event(1, "Next", "10:00", "Room 2");
  zassert_equal(dts()->sched_num, 2);
}

ZTEST(dashboard, test_0x06_calendar_eight_events_fill_every_slot)
{
  static const char *const titles[8] = {"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"};
  uint8_t *pos = put_list_header(8);

  for (int slot = 0; slot < 8; slot++) {
    pos = put_event(pos, titles[slot], "t", "l");
  }
  send_calendar_single(0x11, pos - body);

  expect_chunk_reply(pkt, 0x00);
  for (int slot = 0; slot < 8; slot++) {
    assert_event(slot, titles[slot], "t", "l");
  }
  zassert_equal(dts()->sched_num, 8);
}

ZTEST(dashboard, test_0x06_calendar_ninth_event_keeps_the_stored_schedule)
{
  store_one_old_event();
  send_calendar_single(0x12, put_same_events(9, "N", "t", ""));
  g1_frames_clear();

  assert_event(0, "Old", "1", "");
  assert_events_empty(1);
  zassert_equal(dts()->sched_num, 1);
}

ZTEST(dashboard, test_0x06_calendar_ninth_event_gets_an_error_reply)
{
  send_calendar_single(0x12, put_same_events(9, "N", "t", ""));

  zassert_equal(g1_frame_count(), 1, "%d replies", g1_frame_count());
  expect_chunk_reply(pkt, 0x01);
}

ZTEST(dashboard, test_0x06_calendar_strings_one_below_the_field_sizes_are_stored_whole)
{
  send_calendar_single(0x13, put_long_strings(-1));

  expect_chunk_reply(pkt, 0x00);
  assert_longest_strings();
}

ZTEST(dashboard, test_0x06_calendar_strings_of_the_field_sizes_are_cut_and_terminated)
{
  send_calendar_single(0x14, put_long_strings(0));

  expect_chunk_reply(pkt, 0x00);
  assert_longest_strings();
}

ZTEST(dashboard, test_0x06_calendar_new_list_replaces_every_previous_event)
{
  g1_send_vector("0x06_calendar_two_events");
  send_calendar_single(0x16, put_same_events(1, "Only", "11:00", ""));

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
  /* count 0x0101 */
  zassert_equal(g1_send_vector_with("0x06_calendar_two_events", 6, 0x01), 83);

  expect_frame("06 53 00 0a 03 01 01 01 00 00");
  assert_events_empty(0);
  zassert_equal(dts()->sched_num, 0);
}

/* ---- sub 3: calendar in chunks ---- */

ZTEST(dashboard, test_0x06_calendar_two_chunks_are_acked_chunk_by_chunk)
{
  g1_send_vector("0x06_calendar_chunk1");
  expect_only_frame("06 bd 00 3a 03 02 00 01 00 00");

  g1_send_vector("0x06_calendar_chunk2");
  expect_only_frame("06 6a 00 3b 03 02 00 02 00 00");
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
  send_calendar_chunked(0x20, put_long_strings(-1));

  assert_longest_strings();
}

ZTEST(dashboard, test_0x06_calendar_chunked_strings_of_the_field_sizes_are_cut_and_terminated)
{
  struct schedule_item *item = &schedule_items[0];

  send_calendar_chunked(0x24, put_long_strings(0));

  zassert_equal(item->title[TITLE_SIZE - 1], 0, "title not terminated");
  zassert_equal(item->time[TIME_SIZE - 1], 0, "time not terminated");
  zassert_equal(item->location[LOC_SIZE - 1], 0, "location not terminated");
  assert_longest_strings();
}

ZTEST(dashboard, test_0x06_calendar_chunk_2_without_chunk_1_is_refused_with_status_1)
{
  store_one_old_event();

  g1_send_vector("0x06_calendar_chunk2");

  expect_only_frame("06 6a 00 3b 03 02 00 02 00 01");
  assert_event(0, "Old", "1", "");
  assert_events_empty(1);
}

ZTEST(dashboard, test_0x06_calendar_skipped_chunk_is_refused_and_nothing_is_parsed)
{
  size_t len;

  store_one_old_event();
  len = put_same_events(3, "Event", "12:00-13:00", "Somewhere");

  g1_send(pkt, g1_put_chunk(pkt, 0x06, 0x30, DASH_SUB_CALENDAR, 3, 1, body, 30));
  expect_chunk_reply(pkt, 0x00);
  g1_send(pkt, g1_put_chunk(pkt, 0x06, 0x32, DASH_SUB_CALENDAR, 3, 3, body + 60, len - 60));
  expect_chunk_reply(pkt, 0x01);

  assert_event(0, "Old", "1", "");
  assert_events_empty(1);
}
