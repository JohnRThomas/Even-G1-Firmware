/*
 * Dashboard Set sub 6 (pane mode) and sub 7 (map canvas). Pane mode: 06 07 00 <seq> 06 <mode>
 * <pane>; the reply echoes the first 5 bytes, then status 00. Canvas mode 02: background, <param> +
 * 1 bpp bitmap (376x136 dual, 296x136 full) in 180-byte chunks. Mode 03: 32x32 icon, <x16> <y16> +
 * 128 bytes; it completes the map on a complete background.
 */
#include "dash_test.h"

#define BG_DUAL   (376 * 136 / 8)
#define BG_FULL   (296 * 136 / 8)
#define ICON_SIZE 128
#define ICON_DUAL (4 + BG_DUAL) /* where dual mode stores the icon in panoramic_map_buf */

static uint8_t pkt[256];
static uint8_t canvas[0x1a00];
static uint8_t icon[ICON_SIZE + 8];
static uint8_t dual_bitmap[BG_DUAL];
static uint8_t seq;

/* ---- sub 6: pane mode ---- */

ZTEST(dashboard, test_0x06_pane_mode_stores_mode_and_pane_and_echoes)
{
  static const struct {
    uint8_t seq;
    uint8_t mode;
    uint8_t pane;
  } rows[] = {
    {0x08, 0x01, 0x01},
    {0x08, 0x02, 0x01},
    {0x09, 0x01, 0x03},
    /* pane ids 5 and above are stored as sent */
    {0x08, 0x01, 0x05},
    {0x08, 0x01, 0x09},
  };
  int len = g1_vector("0x06_pane_mode", pkt, sizeof(pkt));

  for (unsigned int row = 0; row < ARRAY_SIZE(rows); row++) {
    char reply[32];

    pkt[3] = rows[row].seq;
    pkt[5] = rows[row].mode;
    pkt[6] = rows[row].pane;
    g1_send(pkt, len);

    snprintf(reply, sizeof(reply), "06 07 00 %02x 06 00", rows[row].seq);
    expect_frame_msg(reply, -1, "row %u", row);
    zassert_equal(dts()->disp_mode, rows[row].mode, "row %u: mode %u", row,
            dts()->disp_mode);
    zassert_equal(dts()->area, rows[row].pane, "row %u: pane %u", row, dts()->area);
    zassert_equal(save_dashboard_ts_to_ncs_mem_fake.call_count, row + 1,
            "row %u: %u saves", row,
            save_dashboard_ts_to_ncs_mem_fake.call_count);
  }
  expect_no_frame();
}

/* the app sends it to each lens itself */
ZTEST(dashboard, test_0x06_pane_mode_is_not_forwarded_to_the_slave)
{
  g1_send_vector("0x06_pane_mode");

  zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.call_count, 0);
  zassert_equal(send_response_data_to_msgqueue_fake.call_count, 0);
  zassert_equal(g1_fake_ipc_send_fake.call_count, 0);
}

ZTEST(dashboard, test_0x06_pane_mode_on_the_slave_is_stored_and_echoed)
{
  g1_unit_set_role(G1_ROLE_SLAVE);

  g1_send_vector("0x06_pane_mode");

  expect_frame("06 07 00 08 06 00");
  zassert_equal(dts()->disp_mode, 1);
  zassert_equal(dts()->area, 1);
}

ZTEST(dashboard, test_0x06_pane_mode_invalid_mode_id_is_rejected)
{
  int len = g1_vector("0x06_pane_mode", pkt, sizeof(pkt));

  g1_send(pkt, len);
  expect_frame("06 07 00 08 06 00");

  pkt[3] = 0x0a;
  pkt[5] = 0x03;
  g1_send(pkt, len);

  zassert_equal(dts()->disp_mode, 1, "mode 3 stored");
  expect_frame("06 07 00 0a 06 01");
}

ZTEST(dashboard, test_0x06_pane_mode_length_mismatch_replies_status_1_and_stores_nothing)
{
  g1_send_vector_with("0x06_pane_mode", 1, 0x08);

  expect_frame_padded("06 08 00 08 06 01", 20);
  zassert_equal(dts()->disp_mode, 0);
  zassert_equal(dts()->area, 0);
  zassert_equal(save_dashboard_ts_to_ncs_mem_fake.call_count, 0);
}

/* ---- sub 7: map canvas ---- */

/* pane mode with the map pane (04) */
static void select_map_pane(uint8_t mode)
{
  const uint8_t request[] = {0x06, 0x07, 0x00, seq, DASH_SUB_PANE_MODE, mode, 0x04};
  const uint8_t reply[] = {0x06, 0x07, 0x00, seq, DASH_SUB_PANE_MODE, 0x00};

  seq++;
  g1_send(request, sizeof(request));
  expect_frame_bytes(reply, sizeof(reply));
}

/* canvas[]: <mode> 04 02 01 + len bytes 00 01 02 ..., in chunks; the last carries last_count */
static uint16_t send_background(uint8_t mode, size_t len, uint16_t count, uint16_t last_count)
{
  const uint8_t header[] = {mode, 0x04, 0x02, 0x01};
  size_t total = sizeof(header) + len;
  uint16_t chunks = DIV_ROUND_UP(total, DASH_CHUNK_DATA);

  memcpy(canvas, header, sizeof(header));
  g1_fill_pattern(canvas + sizeof(header), len, 0);
  for (uint16_t chunk = 1; chunk <= chunks; chunk++) {
    size_t part = MIN(DASH_CHUNK_DATA, total - (chunk - 1) * DASH_CHUNK_DATA);

    g1_send(pkt, g1_put_chunk(pkt, 0x06, seq++, DASH_SUB_CANVAS,
            chunk == chunks ? last_count : count, chunk,
            canvas + (chunk - 1) * DASH_CHUNK_DATA, part));
  }
  return chunks;
}

static void send_good_background(uint8_t mode, size_t len)
{
  uint16_t chunks = DIV_ROUND_UP(4 + len, DASH_CHUNK_DATA);

  send_background(mode, len, chunks, chunks);
  g1_frames_clear();
}

/* mode 03 with len icon bytes 80 81 ... ff 80 (kept in icon[]); returns the packet length */
static size_t make_icon(uint8_t mode, uint16_t x_pos, uint16_t y_pos, size_t len)
{
  uint8_t body[12 + sizeof(icon)] = {mode, 0x04, 0x03};

  sys_put_le16(x_pos, &body[3]);
  sys_put_le16(y_pos, &body[5]);
  for (size_t index = 0; index < len; index++) {
    icon[index] = 0x80 | (index & 0x7f);
  }
  memcpy(body + 7, icon, len);
  return g1_put_chunk(pkt, 0x06, seq++, DASH_SUB_CANVAS, 1, 1, body, 7 + len);
}

static void send_icon(uint8_t mode, uint16_t x_pos, uint16_t y_pos)
{
  g1_send(pkt, make_icon(mode, x_pos, y_pos, ICON_SIZE));
}

ZTEST(dashboard, test_0x06_canvas_mode_0_is_acked_and_stores_the_map_pane)
{
  g1_send_vector("0x06_canvas_mode0");

  expect_only_frame("06 0c 00 13 07 01 00 01 00 00");
  zassert_equal(dts()->disp_mode, 1);
  zassert_equal(dts()->area, 4);
  zassert_equal(dts()->citywalk_mode, 0);
}

/* chunks 2..35 continue the vector's pattern; dual_bitmap gets the bitmap sent */
static void send_dual_background_vectors(void)
{
  uint8_t data[DASH_CHUNK_DATA];
  int len = g1_vector_load("0x06_canvas_bg_chunk1", pkt, sizeof(pkt));

  zassert_equal(len, 189);
  g1_fill_pattern(dual_bitmap, BG_DUAL, 0);
  memcpy(dual_bitmap, pkt + 13, 37);
  g1_send(pkt, len);
  for (int chunk = 2; chunk <= 35; chunk++) {
    g1_fill_pattern(data, sizeof(data), 176 + (chunk - 2) * DASH_CHUNK_DATA);
    g1_send(pkt, g1_put_chunk(pkt, 0x06, 0x1d + chunk, DASH_SUB_CANVAS, 36, chunk, data,
            sizeof(data)));
  }
  len = g1_vector_load("0x06_canvas_bg_last_chunk", pkt, sizeof(pkt));
  zassert_equal(len, 105);
  memcpy(dual_bitmap + 6296, pkt + 9, 41);
  g1_send(pkt, len);
}

ZTEST(dashboard, test_0x06_canvas_dual_background_chunks_are_acked_chunk_by_chunk)
{
  char hex[32];

  seq = 0x10;
  select_map_pane(0x01);
  g1_frames_clear();
  send_dual_background_vectors();

  zassert_equal(g1_frame_count(), 36);
  for (int chunk = 1; chunk <= 35; chunk++) {
    snprintf(hex, sizeof(hex), "06 bd 00 %02x 07 24 00 %02x 00 00", 0x1d + chunk,
       chunk);
    expect_frame(hex);
  }
  expect_frame("06 69 00 43 07 24 00 24 00 00");
}

ZTEST(dashboard, test_0x06_canvas_icon_on_the_dual_background_completes_the_map)
{
  select_map_pane(0x01);
  send_dual_background_vectors();
  g1_frames_clear();
  zassert_equal(dts()->disp_mode, 1);
  zassert_equal(dts()->area, 4);

  send_icon(0x01, 0xac, 0x34);

  expect_chunk_reply(pkt, 0x00);
  zassert_equal(citywalk_canvas_valid, 1);
  zassert_equal(dts()->citywalk_mode, 3);
  zassert_mem_equal(citywalk_canvas_bitmap, dual_bitmap, BG_DUAL);
  zassert_mem_equal(citywalk_arrow_dual, icon, ICON_SIZE);
}

ZTEST(dashboard, test_0x06_canvas_full_background_of_5032_bytes_is_kept_and_completed)
{
  select_map_pane(0x00);
  send_good_background(0x00, BG_FULL);

  zassert_equal(dts()->citywalk_state, 2, "background not kept (state %d)",
          dts()->citywalk_state);
  zassert_mem_equal(panoramic_map_buf + 4, canvas + 4, BG_FULL);

  send_icon(0x00, 0x40, 0x20);
  expect_chunk_reply(pkt, 0x00);
  zassert_equal(citywalk_canvas_valid, 1);
  zassert_mem_equal(citywalk_canvas_bitmap, canvas + 4, BG_FULL);
  zassert_mem_equal(citywalk_arrow_full, icon, ICON_SIZE);
}

ZTEST(dashboard, test_0x06_canvas_dual_background_one_byte_too_many_is_dropped)
{
  select_map_pane(0x01);
  send_good_background(0x01, BG_DUAL + 1);

  zassert_not_equal(dts()->citywalk_state, 2, "background of 6393 bytes kept");
  send_icon(0x01, 0xac, 0x34);
  zassert_equal(citywalk_canvas_valid, 0, "map drawn on a 6393-byte background");
}

ZTEST(dashboard, test_0x06_canvas_dual_background_one_byte_too_few_is_dropped)
{
  select_map_pane(0x01);
  send_good_background(0x01, BG_DUAL - 1);

  zassert_not_equal(dts()->citywalk_state, 2, "background of 6391 bytes kept");
  send_icon(0x01, 0xac, 0x34);
  zassert_equal(citywalk_canvas_valid, 0);
}

ZTEST(dashboard, test_0x06_canvas_full_sized_background_in_dual_mode_is_dropped)
{
  select_map_pane(0x01);
  send_good_background(0x01, BG_FULL);

  send_icon(0x01, 0xac, 0x34);
  zassert_equal(citywalk_canvas_valid, 0, "map drawn on a 5032-byte background in dual mode");
}

ZTEST(dashboard, test_0x06_canvas_dropped_background_is_reported_with_status_1)
{
  uint16_t sent;

  select_map_pane(0x01);
  g1_frames_clear();
  sent = send_background(0x01, BG_DUAL + 1, 36, 36);

  zassert_equal(g1_frame_count(), sent, "%d replies to %u chunks", g1_frame_count(), sent);
  for (int chunk = 1; chunk < sent; chunk++) {
    zassert_equal(g1_frame_peek(chunk - 1, NULL)[9], 0x00, "chunk %d", chunk);
  }
  zassert_equal(g1_frame_peek(sent - 1, NULL)[9], 0x01, "last chunk acked with status 00");
}

ZTEST(dashboard, test_0x06_canvas_chunk_count_mismatch_drops_the_background)
{
  select_map_pane(0x01);
  send_background(0x01, 34 * DASH_CHUNK_DATA + 96 - 4, 36, 35);
  g1_frames_clear();

  zassert_not_equal(dts()->citywalk_state, 2, "background kept");
  send_icon(0x01, 0xac, 0x34);
  zassert_equal(citywalk_canvas_valid, 0);
}

ZTEST(dashboard, test_0x06_canvas_chunk_count_mismatch_gets_an_error_reply)
{
  uint16_t sent;

  select_map_pane(0x01);
  g1_frames_clear();
  sent = send_background(0x01, 34 * DASH_CHUNK_DATA + 96 - 4, 36, 35);

  zassert_equal(g1_frame_count(), sent, "%d replies to %u chunks", g1_frame_count(), sent);
  zassert_equal(g1_frame_peek(sent - 1, NULL)[9], 0x01);
}

/* ---- sub 7: icon ---- */

ZTEST(dashboard, test_0x06_canvas_icon_is_acked_and_stores_x_y_and_its_128_bytes)
{
  int len;

  select_map_pane(0x01);
  len = g1_vector_load("0x06_canvas_icon", pkt, sizeof(pkt));
  zassert_equal(len, 144);
  g1_send(pkt, len);

  expect_only_frame("06 90 00 46 07 01 00 01 00 00");
  zassert_equal(dts()->arrow_x, 0xac);
  zassert_equal(dts()->arrow_y, 0x34);
  zassert_mem_equal(panoramic_map_buf + ICON_DUAL, pkt + 16, ICON_SIZE);
}

ZTEST(dashboard, test_0x06_canvas_icon_odd_x_is_rounded_down)
{
  select_map_pane(0x01);
  send_icon(0x01, 0xad, 0x35);

  expect_chunk_reply(pkt, 0x00);
  zassert_equal(dts()->arrow_x, 0xac);
  zassert_equal(dts()->arrow_y, 0x35);
}

ZTEST(dashboard, test_0x06_canvas_icon_at_the_dual_limits_is_stored)
{
  select_map_pane(0x01);
  send_icon(0x01, 0x158, 0x68);

  expect_chunk_reply(pkt, 0x00);
  zassert_equal(dts()->arrow_x, 0x158);
  zassert_equal(dts()->arrow_y, 0x68);
  zassert_mem_equal(panoramic_map_buf + ICON_DUAL, icon, ICON_SIZE);
}

/* the 32x32 icon fits up to x 376 - 32 = 0x158, y 136 - 32 = 0x68 */
ZTEST(dashboard, test_0x06_canvas_icon_past_the_dual_limits_is_not_stored)
{
  select_map_pane(0x01);
  send_good_background(0x01, BG_DUAL);

  send_icon(0x01, 0x15a, 0x34);
  zassert_equal(citywalk_canvas_valid, 0, "map drawn with the icon at x 0x15a");
  expect_fill(citywalk_arrow_dual, ICON_SIZE, 0, "icon (x 0x15a)");
  send_icon(0x01, 0xac, 0x69);
  zassert_equal(citywalk_canvas_valid, 0, "map drawn with the icon at y 0x69");
  expect_fill(citywalk_arrow_dual, ICON_SIZE, 0, "icon (y 0x69)");
}

/* the 32x32 icon fits up to x 296 - 32 = 0x108, y 136 - 32 = 0x68 */
ZTEST(dashboard, test_0x06_canvas_icon_limits_in_full_mode)
{
  select_map_pane(0x00);
  send_good_background(0x00, BG_FULL);

  send_icon(0x00, 0x10a, 0x10);
  zassert_equal(citywalk_canvas_valid, 0, "map drawn with the icon at x 0x10a");
  expect_fill(citywalk_arrow_full, ICON_SIZE, 0, "icon (x 0x10a)");

  send_icon(0x00, 0x108, 0x68);
  expect_chunk_reply(pkt, 0x00);
  zassert_equal(citywalk_canvas_valid, 1, "map not drawn with the icon at 0x108, 0x68");
  zassert_mem_equal(citywalk_arrow_full, icon, ICON_SIZE);
  zassert_equal(dts()->arrow_x, 0x108);
}

ZTEST(dashboard, test_0x06_canvas_icon_one_byte_too_many_writes_nothing_behind_it)
{
  select_map_pane(0x01);
  g1_send(pkt, make_icon(0x01, 0xac, 0x34, ICON_SIZE + 1));
  g1_frames_clear();

  zassert_equal(panoramic_map_buf[ICON_DUAL + ICON_SIZE], 0,
          "byte 0x%x behind the icon written (0x%02x)", ICON_DUAL + ICON_SIZE,
          panoramic_map_buf[ICON_DUAL + ICON_SIZE]);
}

/* a dual canvas does not fit the full layout */
ZTEST(dashboard, test_0x06_pane_mode_change_puts_the_map_pane_back_to_no_data)
{
  select_map_pane(0x01);
  send_dual_background_vectors();
  send_icon(0x01, 0xac, 0x34);
  g1_frames_clear();
  zassert_equal(dts()->citywalk_mode, 3);

  select_map_pane(0x00);

  zassert_equal(dts()->citywalk_mode, 0);
}
