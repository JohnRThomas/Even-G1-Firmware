/*
 * Dashboard Set sub 7, map canvas. Mode 02: background, <param> + 1 bpp bitmap (376x136 dual,
 * 296x136 full) in 180-byte chunks. Mode 03: 32x32 icon, <x16> <y16> + 128 bytes; it completes the
 * map on a complete background.
 */
#include <stdio.h>

#include "dash_test.h"

#define BG_DUAL      6392 /* 376x136 / 8 */
#define BG_FULL      5032 /* 296x136 / 8 */
#define ICON_SIZE    128
#define ICON_DUAL    0x18fc /* 4 + BG_DUAL */

static uint8_t pkt[256];
static uint8_t canvas[0x1a00];
static uint8_t icon[ICON_SIZE + 8];
static uint8_t seq;

static void select_map_pane(uint8_t mode)
{
	uint8_t body[2] = {mode, 0x04};
	size_t len = dash_single(pkt, seq++, DASH_SUB_PANE_MODE, body, sizeof(body));

	g1_send(pkt, len);
	expect_short_reply(pkt, 0x00);
}

static size_t make_background(uint8_t mode, size_t n)
{
	canvas[0] = mode;
	canvas[1] = 0x04;
	canvas[2] = 0x02;
	canvas[3] = 0x01;
	fill_pattern(canvas + 4, n, 0);
	return 4 + n;
}

/* the last chunk carries last_count */
static uint16_t send_background(size_t len, uint16_t count, uint16_t last_count)
{
	uint16_t n = (len + DASH_CHUNK_DATA - 1) / DASH_CHUNK_DATA;

	for (uint16_t k = 1; k <= n; k++) {
		size_t part = MIN(DASH_CHUNK_DATA, len - (k - 1) * DASH_CHUNK_DATA);
		size_t plen = dash_chunk(pkt, seq++, DASH_SUB_CANVAS, k == n ? last_count : count, k,
					 canvas + (k - 1) * DASH_CHUNK_DATA, part);

		g1_send(pkt, plen);
	}
	return n;
}

static void send_good_background(uint8_t mode, size_t n)
{
	size_t len = make_background(mode, n);
	uint16_t chunks = (len + DASH_CHUNK_DATA - 1) / DASH_CHUNK_DATA;

	send_background(len, chunks, chunks);
	g1_frames_clear();
}

static size_t make_icon(uint8_t mode, uint16_t x, uint16_t y, size_t n)
{
	uint8_t body[12 + sizeof(icon)];

	for (size_t i = 0; i < n; i++) {
		icon[i] = 0x80 | (i & 0x7f);
	}
	body[0] = mode;
	body[1] = 0x04;
	body[2] = 0x03;
	body[3] = x & 0xff;
	body[4] = x >> 8;
	body[5] = y & 0xff;
	body[6] = y >> 8;
	memcpy(body + 7, icon, n);
	return dash_chunk(pkt, seq++, DASH_SUB_CANVAS, 1, 1, body, 7 + n);
}

static void send_icon(uint8_t mode, uint16_t x, uint16_t y)
{
	g1_send(pkt, make_icon(mode, x, y, ICON_SIZE));
}

ZTEST(dashboard, test_0x06_canvas_mode_0_is_acked_and_stores_the_map_pane)
{
	g1_send_vector("0x06_canvas_mode0");

	expect_frame("06 0c 00 13 07 01 00 01 00 00");
	expect_no_frame();
	zassert_equal(dts()->disp_mode, 1);
	zassert_equal(dts()->area, 4);
	zassert_equal(dts()->citywalk_mode, 0);
}

/* chunks 2..35 continue the vector's pattern */
static void send_dual_background_vectors(uint8_t *bitmap_out)
{
	uint8_t data[DASH_CHUNK_DATA];
	int n;

	n = g1_vector_load("0x06_canvas_bg_chunk1", pkt, sizeof(pkt));
	zassert_equal(n, 189);
	fill_pattern(bitmap_out, BG_DUAL, 0);
	memcpy(bitmap_out, pkt + 13, 37);
	g1_send(pkt, n);
	for (int k = 2; k <= 35; k++) {
		size_t len;

		fill_pattern(data, sizeof(data), 176 + (k - 2) * DASH_CHUNK_DATA);
		len = dash_chunk(pkt, 0x1e + k - 1, DASH_SUB_CANVAS, 36, k, data, sizeof(data));
		g1_send(pkt, len);
	}
	n = g1_vector_load("0x06_canvas_bg_last_chunk", pkt, sizeof(pkt));
	zassert_equal(n, 105);
	memcpy(bitmap_out + 6296, pkt + 9, 41);
	g1_send(pkt, n);
}

ZTEST(dashboard, test_0x06_canvas_dual_background_chunks_are_acked_chunk_by_chunk)
{
	static uint8_t bitmap[BG_DUAL];
	char hex[64];

	seq = 0x10;
	select_map_pane(0x01);
	g1_frames_clear();
	send_dual_background_vectors(bitmap);

	zassert_equal(g1_frame_count(), 36);
	expect_frame("06 bd 00 1e 07 24 00 01 00 00");
	for (int k = 2; k <= 35; k++) {
		snprintf(hex, sizeof(hex), "06 bd 00 %02x 07 24 00 %02x 00 00", 0x1e + k - 1, k);
		expect_frame(hex);
	}
	expect_frame("06 69 00 43 07 24 00 24 00 00");
}

ZTEST(dashboard, test_0x06_canvas_icon_on_the_dual_background_completes_the_map)
{
	static uint8_t bitmap[BG_DUAL];

	select_map_pane(0x01);
	send_dual_background_vectors(bitmap);
	g1_frames_clear();
	zassert_equal(dts()->disp_mode, 1);
	zassert_equal(dts()->area, 4);

	send_icon(0x01, 0xac, 0x34);

	expect_chunk_reply(pkt, 0x00);
	zassert_equal(citywalk_canvas_valid, 1);
	zassert_equal(dts()->citywalk_mode, 3);
	zassert_mem_equal(citywalk_canvas_bitmap, bitmap, BG_DUAL);
	zassert_mem_equal(citywalk_arrow_dual, icon, ICON_SIZE);
}

ZTEST(dashboard, test_0x06_canvas_full_background_of_5032_bytes_is_kept_and_completed)
{
	select_map_pane(0x00);
	send_good_background(0x00, BG_FULL);

	zassert_equal(dts()->citywalk_state, 2, "background not kept (state %d)",
		      dts()->citywalk_state);
	expect_pattern(panoramic_map_buf + 4, BG_FULL, 0, "background");

	send_icon(0x00, 0x40, 0x20);
	expect_chunk_reply(pkt, 0x00);
	zassert_equal(citywalk_canvas_valid, 1);
	expect_pattern(citywalk_canvas_bitmap, BG_FULL, 0, "drawn canvas");
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

/* Defect: the canvas case always answers 00. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_canvas_dropped_background_is_reported_with_status_1);
ZTEST(dashboard, test_0x06_canvas_dropped_background_is_reported_with_status_1)
{
	size_t len;
	uint16_t n;
	int frames;

	select_map_pane(0x01);
	g1_frames_clear();
	len = make_background(0x01, BG_DUAL + 1);
	n = send_background(len, 36, 36);

	frames = g1_frame_count();
	zassert_equal(frames, n, "%d replies to %u chunks", frames, n);
	for (int k = 1; k < n; k++) {
		zassert_equal(g1_frame_peek(k - 1, NULL)[9], 0x00, "chunk %d", k);
	}
	zassert_equal(g1_frame_peek(n - 1, NULL)[9], 0x01, "last chunk acked with status 00");
}

ZTEST(dashboard, test_0x06_canvas_chunk_count_mismatch_drops_the_background)
{
	size_t len;

	select_map_pane(0x01);
	len = make_background(0x01, 34 * DASH_CHUNK_DATA + 96 - 4);
	send_background(len, 36, 35);
	g1_frames_clear();

	zassert_not_equal(dts()->citywalk_state, 2, "background kept");
	send_icon(0x01, 0xac, 0x34);
	zassert_equal(citywalk_canvas_valid, 0);
}

/* Defect: a packet order error returns before the reply. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_canvas_chunk_count_mismatch_gets_an_error_reply);
ZTEST(dashboard, test_0x06_canvas_chunk_count_mismatch_gets_an_error_reply)
{
	size_t len;
	uint16_t n;

	select_map_pane(0x01);
	g1_frames_clear();
	len = make_background(0x01, 34 * DASH_CHUNK_DATA + 96 - 4);
	n = send_background(len, 36, 35);

	zassert_equal(g1_frame_count(), n, "%d replies to %u chunks", g1_frame_count(), n);
	zassert_equal(g1_frame_peek(n - 1, NULL)[9], 0x01);
}

/* ---- icon ---- */

ZTEST(dashboard, test_0x06_canvas_icon_is_acked_and_stores_x_y_and_its_128_bytes)
{
	uint8_t v[144];
	int n = g1_vector_load("0x06_canvas_icon", v, sizeof(v));

	zassert_equal(n, 144);
	select_map_pane(0x01);
	g1_send(v, n);

	expect_frame("06 90 00 46 07 01 00 01 00 00");
	expect_no_frame();
	zassert_equal(dts()->arrow_x, 0xac);
	zassert_equal(dts()->arrow_y, 0x34);
	zassert_mem_equal(panoramic_map_buf + ICON_DUAL, v + 16, ICON_SIZE);
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
	expect_all(citywalk_arrow_dual, ICON_SIZE, 0, "icon (x 0x15a)");
	send_icon(0x01, 0xac, 0x69);
	zassert_equal(citywalk_canvas_valid, 0, "map drawn with the icon at y 0x69");
	expect_all(citywalk_arrow_dual, ICON_SIZE, 0, "icon (y 0x69)");
}

/* the 32x32 icon fits up to x 296 - 32 = 0x108, y 136 - 32 = 0x68 */
ZTEST(dashboard, test_0x06_canvas_icon_limits_in_full_mode)
{
	select_map_pane(0x00);
	send_good_background(0x00, BG_FULL);

	send_icon(0x00, 0x10a, 0x10);
	zassert_equal(citywalk_canvas_valid, 0, "map drawn with the icon at x 0x10a");
	expect_all(citywalk_arrow_full, ICON_SIZE, 0, "icon (x 0x10a)");

	send_icon(0x00, 0x108, 0x68);
	expect_chunk_reply(pkt, 0x00);
	zassert_equal(citywalk_canvas_valid, 1, "map not drawn with the icon at 0x108, 0x68");
	zassert_mem_equal(citywalk_arrow_full, icon, ICON_SIZE);
	zassert_equal(dts()->arrow_x, 0x108);
}

/* Defect: data_len - 0x10 bytes are copied, however many. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_canvas_icon_one_byte_too_many_writes_nothing_behind_it);
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
	static uint8_t bitmap[BG_DUAL];

	select_map_pane(0x01);
	send_dual_background_vectors(bitmap);
	send_icon(0x01, 0xac, 0x34);
	g1_frames_clear();
	zassert_equal(dts()->citywalk_mode, 3);

	select_map_pane(0x00);

	zassert_equal(dts()->citywalk_mode, 0);
}
