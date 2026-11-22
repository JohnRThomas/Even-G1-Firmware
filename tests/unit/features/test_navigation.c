/*
 * 0x0A navigation: 0A len16 seq sub; sub 0 init, 1 trip status, 2 overview map, 3 panoramic map, 4
 * sync, 5 exit, 6 arrived. On the master, init / trip / last-chunk / arrived replies are queued for
 * the phone.
 */
#include "features.h"

#define OVERVIEW_SIZE 0x1210 /* sizeof(nav_overview_map_buf) */
#define OV_HDR        9
#define PANO_HDR      10
#define CHUNK_DATA    185 /* map bytes per 194 / 195-byte packet */

static struct nav_info *nav(void) {
  return g1_state()->nav;
}

static void send_map_chunk(uint8_t sub, uint8_t seq, uint16_t total, uint16_t index, const uint8_t *data, size_t len) {
  uint8_t pkt[256];
  uint8_t payload[sizeof(pkt) - G1_CHUNK_HDR_LEN] = {0};
  size_t flag_len = sub == 3 ? 1 : 0; /* sub 3 starts with the panoramic flag 00 */

  memcpy(&payload[flag_len], data, len);
  g1_send(pkt, g1_put_chunk(pkt, 0x0a, seq, sub, total, index, payload, flag_len + len));
}

/* chunks 1..count of the map pattern of the vectors (byte k = k & 0xff); returns the bytes sent */
static size_t send_map_chunks(uint8_t sub, uint16_t total, uint16_t count) {
  uint8_t data[CHUNK_DATA];

  for (uint16_t index = 1; index <= count; index++) {
    g1_fill_pattern(data, CHUNK_DATA, (size_t)(index - 1) * CHUNK_DATA);
    send_map_chunk(sub, (uint8_t)index, total, index, data, CHUNK_DATA);
  }
  return (size_t)count * CHUNK_DATA;
}

/* send_map_chunks(), then chunk count + 1 with one byte more than fits in size */
static size_t send_map_one_byte_over(uint8_t sub, uint8_t seq, uint16_t total, uint16_t count, size_t size) {
  uint8_t data[CHUNK_DATA];
  size_t at = send_map_chunks(sub, total, count);

  memset(data, 0xee, sizeof(data));
  send_map_chunk(sub, seq, total, count + 1, data, size - at + 1);
  return at;
}

/* sends vector name and copies its map bytes (after hdr) to want[at]; returns the new end */
static size_t send_map_vector(const char *name, size_t hdr, uint8_t *want, size_t at) {
  uint8_t pkt[256];
  size_t len = g1_vector(name, pkt, sizeof(pkt));

  g1_send(pkt, len);
  memcpy(&want[at], &pkt[hdr], len - hdr);
  return at + len - hdr;
}

/* sub 1: direction 2, x 0x10, y 0x20, then five NUL-terminated strings */
static void send_trip(uint8_t seq, const char *const strings[5]) {
  uint8_t pkt[160];
  size_t len = 10;

  pkt[0] = 0x0a;
  pkt[3] = seq;
  pkt[4] = 0x01;
  pkt[5] = 0x02;
  sys_put_le16(0x10, &pkt[6]);
  sys_put_le16(0x20, &pkt[8]);
  for (int index = 0; index < 5; index++) {
    size_t size = strlen(strings[index]) + 1;

    memcpy(&pkt[len], strings[index], size);
    len += size;
  }
  g1_send(pkt, feat_set_len16(pkt, len));
}

/* task 10 = navigation */
ZTEST(features, test_0x0a_init_starts_navigation) {
  g1_send_vector("0x0a_init");

  zassert_equal(nav()->running, 1);
  zassert_equal(nav()->sync_id, 0x6d);
  zassert_equal(nav()->type, 0);
  zassert_equal(g1_state()->esb_down_id, 0x6d);
  zassert_equal(update_persist_task_status_fake.call_count, 1);
  zassert_equal(update_persist_task_status_fake.arg1_val, 10);
  feat_expect_queued_count(1);
  expect_pkt(&g1_esb_response_log, 0, "0a 06 00 6d 00 00");
  expect_no_frame();
}

ZTEST(features, test_0x0a_init_reply_is_written_at_once_on_the_slave) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x0a_init");

  expect_only_frame("0a 06 00 6d 00 00");
}

ZTEST(features, test_0x0a_init_clears_a_previous_trip) {
  strcpy(nav()->road_name, "Old Road");
  nav()->arrived = 2;
  nav()->map_size = 1234;
  nav_overview_map_ready = 1;
  nav_panoramic_map_ready = 1;

  g1_send_vector("0x0a_init");

  feat_expect_field(nav()->road_name, sizeof(nav()->road_name), "");
  zassert_equal(nav()->arrived, 0);
  zassert_equal(nav()->map_size, 0);
  zassert_equal(nav_overview_map_ready, 0);
  zassert_equal(nav_panoramic_map_ready, 0);
}

ZTEST(features, test_0x0a_trip_status_sets_icon_and_x_y_as_u16) {
  g1_send_vector("0x0a_trip_status");

  zassert_equal(nav()->direction, 3);
  zassert_equal(nav()->x, 211);
  zassert_equal(nav()->y, 123);
  zassert_equal(nav_pos_updated, 1);
}

ZTEST(features, test_0x0a_trip_status_fills_five_strings) {
  g1_send_vector("0x0a_trip_status");

  feat_expect_field(nav()->time_remaining, sizeof(nav()->time_remaining), "15 min");
  feat_expect_field(nav()->km_remaining, sizeof(nav()->km_remaining), "5km");
  feat_expect_field(nav()->road_name, sizeof(nav()->road_name), "Easy Street ");
  feat_expect_field(nav()->remaining_distance, sizeof(nav()->remaining_distance), "111m");
  feat_expect_field(nav()->speed, sizeof(nav()->speed), "0.0km/h");
  zassert_equal(nav()->updated, 1);
  zassert_equal(nav()->type, 1);
  zassert_equal(nav()->sync_id, 0x75);
  zassert_equal(nav_text_updated, 1);
}

ZTEST(features, test_0x0a_trip_status_reply_is_queued_on_the_master) {
  g1_send_vector("0x0a_trip_status");

  expect_pkt(&g1_esb_response_log, 0, "0a 2f 00 75 01 00");
  expect_no_frame();
}

ZTEST(features, test_0x0a_trip_status_reply_is_written_at_once_on_the_slave) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x0a_trip_status");

  expect_frame("0a 2f 00 75 01 00");
}

/* a scan past the packet would find the harness's zeroed rx bytes */
ZTEST(features, test_0x0a_trip_status_missing_terminator_is_rejected) {
  uint8_t pkt[64];
  size_t len = g1_vector("0x0a_trip_status", pkt, sizeof(pkt));

  g1_send(pkt, feat_set_len16(pkt, len - 1));

  expect_frame("0a 2e 00 75 01 01");
  zassert_equal(nav()->updated, 0);
  zassert_equal(nav()->type, 0);
  zassert_equal(nav_text_updated, 0);
  feat_expect_field(nav()->speed, sizeof(nav()->speed), "");
}

ZTEST(features, test_0x0a_trip_status_road_name_of_63_chars_fits) {
  static char road[64];
  const char *strings[5] = {"1 min", "1km", road, "10m", "5km/h"};

  memset(road, 'R', 63);
  g1_unit_set_role(G1_ROLE_SLAVE);
  send_trip(0x40, strings);

  feat_expect_field(nav()->road_name, sizeof(nav()->road_name), road);
  feat_expect_field(nav()->speed, sizeof(nav()->speed), "5km/h");
  expect_frame("0a 5e 00 40 01 00");
}

ZTEST(features, test_0x0a_trip_status_road_name_of_64_chars_is_rejected) {
  static char road[65];
  const char *strings[5] = {"1 min", "1km", road, "10m", "5km/h"};

  memset(road, 'R', 64);
  g1_unit_set_role(G1_ROLE_SLAVE);
  send_trip(0x41, strings);

  expect_frame("0a 5f 00 41 01 01");
  feat_expect_field(nav()->road_name, sizeof(nav()->road_name), "");
  zassert_equal(nav()->updated, 0);
}

/* 8 x 185 + 108 = 1588 */
ZTEST(features, test_0x0a_overview_map_chunks_are_assembled_in_nav_overview_map_buf) {
  static uint8_t want[OVERVIEW_SIZE];
  size_t at = send_map_vector("0x0a_overview_map_chunk1", OV_HDR, want, 0);

  for (uint16_t index = 2; index <= 8; index++) {
    g1_fill_pattern(&want[at], CHUNK_DATA, at);
    send_map_chunk(2, 0x78 + index, 9, index, &want[at], CHUNK_DATA);
    at += CHUNK_DATA;
  }
  zassert_equal(nav_overview_map_ready, 0, "ready before the last chunk");
  at = send_map_vector("0x0a_overview_map_last_chunk", OV_HDR, want, at);

  zassert_equal(at, 1588);
  zassert_mem_equal(nav_overview_map_buf, want, sizeof(want));
  zassert_equal(nav()->map_size, 1588);
  zassert_equal(nav_overview_map_ready, 1);
  zassert_equal(nav()->type, 2);
  zassert_equal(nav()->sync_id, 0x80);
}

ZTEST(features, test_0x0a_overview_map_acks_the_first_and_the_last_chunk) {
  uint8_t data[CHUNK_DATA] = {0};

  g1_send_vector("0x0a_overview_map_chunk1");
  expect_frame("0a c2 00 78 02 09 00 01 00 00");
  for (uint16_t index = 2; index <= 8; index++) {
    send_map_chunk(2, 0x78 + index, 9, index, data, CHUNK_DATA);
  }
  expect_no_frame();
  g1_send_vector("0x0a_overview_map_last_chunk");

  expect_no_frame();
  feat_expect_queued_count(1);
  expect_pkt(&g1_esb_response_log, 0, "0a 75 00 80 02 09 00 09 00 00");
}

ZTEST(features, test_0x0a_overview_map_chunk_out_of_order_replies_01) {
  uint8_t data[CHUNK_DATA];

  g1_send_vector("0x0a_overview_map_chunk1");
  expect_frame("0a c2 00 78 02 09 00 01 00 00");
  g1_fill_pattern(data, CHUNK_DATA, 0);
  send_map_chunk(2, 0x7a, 9, 3, data, CHUNK_DATA);

  expect_frame("0a c2 00 7a 02 09 00 03 00 01");
  zassert_equal(multi_packet_cnt, 0);
  zassert_equal(multi_packet_len, 0);
  zassert_equal(nav_overview_map_buf[0], 0, "map not dropped");
}

/* 24 x 185 + 184 = 0x1210 */
ZTEST(features, test_0x0a_overview_map_filling_the_buffer_exactly_is_accepted) {
  static uint8_t want[OVERVIEW_SIZE];
  size_t at = send_map_chunks(2, 25, 24);

  g1_fill_pattern(want, sizeof(want), 0);
  send_map_chunk(2, 0x99, 25, 25, &want[at], OVERVIEW_SIZE - at);

  zassert_equal(nav_overview_map_ready, 1);
  zassert_equal(nav()->map_size, OVERVIEW_SIZE);
  zassert_mem_equal(nav_overview_map_buf, want, sizeof(want));
}

ZTEST(features, test_0x0a_overview_map_last_chunk_one_byte_over_is_rejected) {
  size_t at = send_map_one_byte_over(2, 0x99, 25, 24, OVERVIEW_SIZE);

  zassert_equal(nav_overview_map_ready, 0);
  zassert_equal(nav()->map_size, 0);
  zassert_equal(multi_packet_len, at);
  expect_fill(&nav_overview_map_buf[at], OVERVIEW_SIZE - at, 0, "overview map tail");
  feat_expect_queued_count(0);
}

ZTEST(features, test_0x0a_overview_map_middle_chunk_one_byte_over_is_rejected) {
  size_t at = send_map_one_byte_over(2, 0x99, 26, 24, OVERVIEW_SIZE);

  zassert_equal(multi_packet_len, at);
  zassert_equal(multi_packet_cnt, 24);
  expect_fill(&nav_overview_map_buf[at], OVERVIEW_SIZE - at, 0, "overview map tail");
}

/* 89 x 185 + 127 = 0x40d0 */
ZTEST(features, test_0x0a_panoramic_chunks_fill_panoramic_map_buf) {
  static uint8_t want[PANORAMIC_MAP_SIZE];
  size_t at;

  nav()->panoramic = 0x55;
  at = send_map_vector("0x0a_panoramic_chunk1", PANO_HDR, want, 0);
  zassert_equal(nav()->panoramic, 0x00, "flag of chunk 1");
  for (uint16_t index = 2; index <= 89; index++) {
    g1_fill_pattern(&want[at], CHUNK_DATA, at);
    send_map_chunk(3, (uint8_t)(0x81 + index - 1), 90, index, &want[at], CHUNK_DATA);
    at += CHUNK_DATA;
  }
  zassert_equal(nav_panoramic_map_ready, 0, "ready before the last chunk");
  at = send_map_vector("0x0a_panoramic_last_chunk", PANO_HDR, want, at);

  zassert_equal(at, PANORAMIC_MAP_SIZE);
  zassert_mem_equal(panoramic_map_buf, want, sizeof(want));
  zassert_equal(nav_panoramic_map_ready, 1);
  zassert_equal(nav()->type, 3);
  zassert_equal(nav()->sync_id, 0xda);
}

ZTEST(features, test_0x0a_panoramic_acks_the_first_and_the_last_chunk) {
  g1_send_vector("0x0a_panoramic_chunk1");
  expect_frame("0a c3 00 81 03 5a 00 01 00 00 00");
  for (uint16_t index = 2; index <= 89; index++) {
    uint8_t data[CHUNK_DATA] = {0};

    send_map_chunk(3, 0x82, 90, index, data, CHUNK_DATA);
  }
  expect_no_frame();
  g1_send_vector("0x0a_panoramic_last_chunk");

  expect_no_frame();
  expect_pkt(&g1_esb_response_log, 0, "0a 89 00 da 03 5a 00 5a 00 00 00");
}

ZTEST(features, test_0x0a_panoramic_last_ack_is_written_at_once_on_the_slave) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  send_map_chunks(3, 90, 89);
  g1_frames_clear();
  g1_send_vector("0x0a_panoramic_last_chunk");

  expect_frame("0a 89 00 da 03 5a 00 5a 00 00 00");
}

ZTEST(features, test_0x0a_panoramic_last_chunk_one_byte_over_is_rejected) {
  size_t at = send_map_one_byte_over(3, 0xda, 90, 89, PANORAMIC_MAP_SIZE);

  zassert_equal(nav_panoramic_map_ready, 0);
  expect_fill(&panoramic_map_buf[at], PANORAMIC_MAP_SIZE - at, 0, "panoramic map tail");
  feat_expect_queued_count(0);
}

ZTEST(features, test_0x0a_panoramic_middle_chunk_one_byte_over_is_rejected) {
  size_t at = send_map_one_byte_over(3, 0xda, 91, 89, PANORAMIC_MAP_SIZE);

  zassert_equal(nav_panoramic_map_ready, 0);
  expect_fill(&panoramic_map_buf[at], PANORAMIC_MAP_SIZE - at, 0, "panoramic map tail");
}

/* The reply echoes seq and beat. The app steps the beat by 2, through the 0xff wrap. */
ZTEST(features, test_0x0a_sync_is_counted_and_echoed_while_navigating) {
  static const struct {
    uint8_t seq;
    uint8_t beat;
  } rows[] = {
    {0x6e, 0x01}, {0x70, 0xfb}, {0x71, 0xfd}, {0x72, 0xff}, {0x73, 0x01}, {0x74, 0x03},
  };
  uint8_t pkt[8];
  size_t len = g1_vector("0x0a_sync", pkt, sizeof(pkt));

  g1_send_vector("0x0a_init");
  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    char want[32];

    pkt[3] = rows[row].seq;
    pkt[5] = rows[row].beat;
    g1_send(pkt, len);

    snprintf(want, sizeof(want), "0a 06 00 %02x 04 %02x", rows[row].seq, rows[row].beat);
    expect_frame_msg(want, -1, "seq 0x%02x, beat 0x%02x", rows[row].seq, rows[row].beat);
    zassert_equal(nav_app_sync_count, row + 1, "seq 0x%02x, beat 0x%02x: count %d, want %u", rows[row].seq,
                  rows[row].beat, nav_app_sync_count, (unsigned int)(row + 1));
  }
  expect_no_frame();
}

ZTEST(features, test_0x0a_sync_without_navigation_is_not_counted) {
  g1_send_vector("0x0a_sync");

  expect_frame("0a 06 00 6e 04 00");
  zassert_equal(nav_app_sync_count, 0);
}

ZTEST(features, test_0x0a_exit_stops_navigation) {
  g1_send_vector("0x0a_init");
  g1_send_vector("0x0a_trip_status");
  g1_send_vector("0x0a_exit");

  expect_fill(nav(), sizeof(struct nav_info), 0, "nav_info");
  expect_only_frame("0a 06 00 b2 05 01");
}

ZTEST(features, test_0x0a_arrived_stores_status_and_prompt) {
  g1_send_vector("0x0a_arrived");

  zassert_equal(nav()->arrived, 2);
  zassert_equal(nav()->type, 6);
  zassert_equal(nav()->updated, 1);
  zassert_equal(nav()->sync_id, 0xae);
  feat_expect_field(nav()->prompt, sizeof(nav()->prompt), "Navigation Complete");
  expect_pkt(&g1_esb_response_log, 0, "0a 1a 00 ae 06 00");
  expect_no_frame();
}

ZTEST(features, test_0x0a_arrived_reply_is_written_at_once_on_the_slave) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  g1_send_vector("0x0a_arrived");

  expect_frame("0a 1a 00 ae 06 00");
}

/* prompt is 0x40 bytes; 64 chars leave no terminator */
ZTEST(features, test_0x0a_arrived_prompt_bound) {
  uint8_t pkt[96] = {0x0a, 0x00, 0x00, 0x30, 0x06, 0x02};

  g1_unit_set_role(G1_ROLE_SLAVE);
  memset(&pkt[6], 'P', 62);
  pkt[6 + 62] = '\0';
  g1_send(pkt, feat_set_len16(pkt, 6 + 63));
  expect_frame("0a 45 00 30 06 00");
  zassert_equal(strlen(nav()->prompt), 62);

  memset(&pkt[6], 'Q', 64);
  pkt[3] = 0x31;
  g1_send(pkt, feat_set_len16(pkt, 6 + 64));
  expect_frame("0a 46 00 31 06 01");
  feat_expect_field(nav()->prompt, sizeof(nav()->prompt), "");
}
