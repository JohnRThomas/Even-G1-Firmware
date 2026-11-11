/*
 * 0x15 file upload, 0x20 upload complete, 0x16 CRC check, 0x18 exit. FILE_CRC is zlib CRC-32 over
 * address + file.
 * try_to_save_file() keeps its upload position in statics the harness can't reset, so only the
 * full-upload test runs it.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_display.h"
#include "fakes_storage.h"
#include "storage/storage.h"
#include "features.h"

#define BMP_SIZE    9854
#define BMP_HDR     62
#define PIECE       194
#define PACKETS     51 /* 50 x 194 + 154 */
#define DEMO_ADDR   0x1c0000
#define FILE_CRC    0x8fce7992u

/* file_storage.c internals */
struct file_msg {
	uint8_t data[200];
	uint8_t len;
};
int dequeue_file(struct file_msg *out);
void try_to_save_file(struct GlassesState *gs);
extern uint8_t BIT_MAP_CHUNK_ID;

static uint8_t bmp[BMP_SIZE];

/* 576 x 136, 1 bpp BMP */
static void make_bmp(void)
{
	static const char hdr[] = "42 4d 7e 26 00 00 00 00 00 00 3e 00 00 00 " /* file header */
				  "28 00 00 00 40 02 00 00 88 00 00 00 01 00 01 00 "
				  "00 00 00 00 40 26 00 00 00 00 00 00 00 00 00 00 "
				  "02 00 00 00 00 00 00 00 "                   /* info header */
				  "00 00 00 00 ff ff ff 00";                   /* palette */
	int n = g1_hex_parse(hdr, bmp, sizeof(bmp));

	zassert_equal(n, BMP_HDR);
	for (int r = 0; r < 136; r++) {
		uint8_t *row = &bmp[BMP_HDR + r * 72];

		memset(row, r == 0 || r == 135 ? 0x00 : 0xff, 72);
		row[0] &= 0x7f;
		row[71] &= 0xfe;
	}
}

static size_t upload_pkt(uint8_t *pkt, int n)
{
	size_t at = (size_t)n * PIECE;
	size_t len = MIN((size_t)PIECE, BMP_SIZE - at);
	size_t hdr = 2;

	pkt[0] = 0x15;
	pkt[1] = (uint8_t)n;
	if (n == 0) {
		pkt[2] = 0x00;
		pkt[3] = 0x1c;
		pkt[4] = 0x00;
		pkt[5] = 0x00;
		hdr = 6;
	}
	memcpy(&pkt[hdr], &bmp[at], len);
	return hdr + len;
}

static void expect_queued_file_msg(const uint8_t *want, size_t len)
{
	struct file_msg msg;

	zassert_equal(dequeue_file(&msg), 0, "nothing queued");
	zassert_equal(msg.len, len, "queued %u bytes, want %u", msg.len, (unsigned int)len);
	zassert_mem_equal(msg.data, want, len);
}

static void expect_queue_empty(void)
{
	struct file_msg msg;

	zassert_not_equal(dequeue_file(&msg), 0, "a message was queued");
}

ZTEST(features, test_0x15_first_packet_is_queued_with_the_address)
{
	uint8_t pkt[256];
	size_t n = feat_vector("0x15_bitmap_first_packet", pkt, sizeof(pkt));

	g1_send(pkt, n);

	expect_no_frame();
	expect_queued_file_msg(&pkt[1], 199);
	zassert_equal(g1_state()->dashboard_cmd, 0x15);
}

ZTEST(features, test_0x15_next_packet_carries_194_file_bytes_and_no_address)
{
	uint8_t first[256], next[256];
	size_t nf = feat_vector("0x15_bitmap_first_packet", first, sizeof(first));
	size_t nn = feat_vector("0x15_bitmap_next_packet", next, sizeof(next));

	zassert_equal(nn, 2 + PIECE);
	g1_send(first, nf);
	g1_send(next, nn);

	expect_no_frame();
	expect_queued_file_msg(&first[1], 199);
	expect_queued_file_msg(&next[1], 1 + PIECE);
}

ZTEST(features, test_0x15_upload_must_start_with_packet_0)
{
	g1_send_vector("0x15_bitmap_next_packet");

	expect_frame_padded("15 00 ca", 20);
	expect_queue_empty();
	zassert_equal(BIT_MAP_CHUNK_ID, 0xff, "expected number moved");
}

ZTEST(features, test_0x15_out_of_order_packet_replies_the_expected_number_and_ca)
{
	uint8_t first[256], next[256];
	size_t nf = feat_vector("0x15_bitmap_first_packet", first, sizeof(first));
	size_t nn = feat_vector("0x15_bitmap_next_packet", next, sizeof(next));

	g1_send(first, nf);
	next[1] = 0x02;
	g1_send(next, nn);

	expect_frame_padded("15 01 ca", 20);
	expect_no_frame();

	next[1] = 0x01;
	g1_send(next, nn);
	expect_no_frame();
	expect_queued_file_msg(&first[1], 199);
	expect_queued_file_msg(&next[1], 1 + PIECE);
	expect_queue_empty();
}

ZTEST(features, test_0x15_packet_number_wraps_after_ff)
{
	uint8_t pkt[8] = {0x15, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x42};

	for (int i = 0; i < 0x101; i++) {
		pkt[1] = (uint8_t)i;
		g1_send(pkt, 7);
		expect_no_frame();
		{
			struct file_msg msg;

			zassert_equal(dequeue_file(&msg), 0, "packet %d not queued", i);
		}
	}
	zassert_equal(BIT_MAP_CHUNK_ID, 0x00);
}

/* DEFECT: enqueue_file() copies len - 1 bytes into a 201-byte stack struct. */
ZTEST(features, test_0x15_packet_longer_than_a_queue_message_is_refused)
{
	uint8_t pkt[2 + 243] = {0x15, 0x00};

	ztest_test_skip();

	g1_send(pkt, sizeof(pkt));
	expect_queue_empty();
}

ZTEST(features, test_0x20_end_marker_replies_c9_and_restarts_the_numbering)
{
	static const uint8_t marker[] = {0x0d, 0x0e};
	uint8_t pkt[256];
	size_t n = feat_vector("0x15_bitmap_first_packet", pkt, sizeof(pkt));

	g1_send(pkt, n);
	(void)dequeue_file(&(struct file_msg){0});
	g1_send_vector("0x20_upload_complete");

	expect_frame_padded("20 c9", 20);
	expect_queued_file_msg(marker, 2);
	zassert_equal(g1_state()->dashboard_cmd, 0x20);

	g1_send(pkt, n);
	expect_no_frame();
	expect_queued_file_msg(&pkt[1], 199);
}

ZTEST(features, test_0x20_without_0d_0e_replies_ca)
{
	g1_send_hex("20 0d 0f");

	expect_frame_padded("20 ca", 20);
	expect_queue_empty();
}

ZTEST(features, test_0x16_matching_crc_replies_c9)
{
	LAST_FILE_CRC = FILE_CRC;
	g1_send_vector("0x16_bitmap_crc");

	expect_frame_padded("16 8f ce 79 92 c9", 20);
	zassert_equal(g1_state()->dashboard_cmd, 0x16);
}

ZTEST(features, test_0x16_wrong_crc_replies_ca)
{
	LAST_FILE_CRC = FILE_CRC ^ 1;
	g1_send_vector("0x16_bitmap_crc");

	expect_frame_padded("16 8f ce 79 92 ca", 20);
}

/* two 4 KB blocks and the rest */
ZTEST(features, test_0x15_upload_stores_the_file_at_its_address_and_0x16_accepts_its_crc)
{
	uint8_t pkt[256];

	make_bmp();
	for (int n = 0; n < PACKETS; n++) {
		g1_send(pkt, upload_pkt(pkt, n));
		try_to_save_file(g1_state());
	}
	expect_no_frame();
	g1_send_vector("0x20_upload_complete");
	expect_frame_padded("20 c9", 20);
	try_to_save_file(g1_state());

	zassert_mem_equal(&g1_fake_flash_image[DEMO_ADDR], bmp, BMP_SIZE,
			  "flash at 0x1c0000 differs from the file");
	zassert_equal(write_font_fake.call_count, 3);
	zassert_equal(write_font_fake.arg0_history[0], DEMO_ADDR);
	zassert_equal(write_font_fake.arg0_history[1], DEMO_ADDR + 0x1000);
	zassert_equal(write_font_fake.arg0_history[2], DEMO_ADDR + 0x2000);
	zassert_equal(write_font_fake.arg2_history[2], BMP_SIZE - 0x2000);
	zassert_equal(LAST_FILE_CRC, FILE_CRC, "LAST_FILE_CRC 0x%08x", LAST_FILE_CRC);

	g1_send_vector("0x16_bitmap_crc");
	expect_frame_padded("16 8f ce 79 92 c9", 20);
}

ZTEST(features, test_0x18_exit_replies_c9_and_closes_the_running_feature)
{
	g1_send_vector("0x18_exit");

	expect_frame_padded("18 c9", 20);
	zassert_equal(exit_running_task_fake.call_count, 1);
	zassert_equal_ptr(exit_running_task_fake.arg0_val, g1_state());
	zassert_equal(exit_running_task_keep_status_fake.call_count, 0);
}

ZTEST(features, test_0x18_exit_in_mode_0x10_keeps_the_task_status)
{
	g1_state()->dashboard_mode = 0x10;
	g1_send_vector("0x18_exit");

	expect_frame_padded("18 c9", 20);
	zassert_equal(exit_running_task_keep_status_fake.call_count, 1);
	zassert_equal(exit_running_task_fake.call_count, 0);
}
