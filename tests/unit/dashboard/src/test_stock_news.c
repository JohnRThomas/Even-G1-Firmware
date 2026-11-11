/*
 * Dashboard Set sub 4 (stocks) and sub 5 (news). Item body: <dash mode> <pane> 02 <item 1..4>
 * <action 01 set / 02 delete> <TLVs>.
 */
#include "dash_test.h"
#include "fakes_display.h"

#define STOCK_IMAGE_MAX 0x75e
#define NEWS_TEXT_MAX   0x118

static uint8_t pkt[256];
static uint8_t body[0x900];
static uint8_t image[0x800];
static char text[0x200];

static uint8_t *put_tlv8(uint8_t *p, uint8_t type, const void *s, size_t n)
{
	*p++ = type;
	*p++ = (uint8_t)n;
	memcpy(p, s, n);
	return p + n;
}

static uint8_t *put_tlv16(uint8_t *p, uint8_t type, const void *s, size_t n, uint16_t len)
{
	*p++ = type;
	*p++ = len & 0xff;
	*p++ = len >> 8;
	memcpy(p, s, n);
	return p + n;
}

/* img_len is declared, n_img bytes are sent */
static size_t stock_body(uint8_t num, const char *code, uint16_t img_len, size_t n_img)
{
	uint8_t *p = body;

	*p++ = 0x00;
	*p++ = 0x01;
	*p++ = 0x02;
	*p++ = num;
	*p++ = 0x01;
	p = put_tlv8(p, 1, code, strlen(code));
	p = put_tlv8(p, 2, "GameStop Corp", 13);
	p = put_tlv8(p, 3, "21.36", 5);
	p = put_tlv8(p, 4, "-0.09", 5);
	p = put_tlv8(p, 5, "-0.42%", 6);
	p = put_tlv8(p, 6, "\x02", 1);
	p = put_tlv16(p, 7, image, n_img, img_len);
	return p - body;
}

static void assert_stock(int i, const char *code, uint16_t img_len)
{
	struct stock_item *st = &stock_items[i];

	zassert_equal(st->valid, 1, "stock %d not valid", i);
	zassert_true(strcmp(st->code, code) == 0, "stock %d code %.32s", i, st->code);
	zassert_true(strcmp(st->company, "GameStop Corp") == 0, "company %.64s", st->company);
	zassert_true(strcmp(st->price, "21.36") == 0, "price %.16s", st->price);
	zassert_true(strcmp(st->price_change, "-0.09") == 0, "change %.16s", st->price_change);
	zassert_true(strcmp(st->change_rate, "-0.42%") == 0, "rate %.16s", st->change_rate);
	zassert_equal(st->change_status, 2);
	zassert_equal(st->image_len, img_len, "image_len %u", st->image_len);
	zassert_mem_equal(st->image, image, img_len);
}

static void assert_stock_empty(int i)
{
	static const struct stock_item zero;

	zassert_mem_equal(&stock_items[i], &zero, sizeof(zero), "stock %d not empty", i);
}

static void send_single(uint8_t sub, uint8_t seq, size_t n)
{
	size_t len = dash_chunk(pkt, seq, sub, 1, 1, body, n);

	zassert_true(len <= sizeof(pkt));
	g1_send(pkt, len);
}

/* the last chunk's reply stays queued */
static void send_chunked(uint8_t sub, uint8_t seq, size_t n)
{
	uint16_t total = (n + DASH_CHUNK_DATA - 1) / DASH_CHUNK_DATA;

	for (uint16_t i = 0; i < total; i++) {
		size_t part = MIN(DASH_CHUNK_DATA, n - i * DASH_CHUNK_DATA);
		size_t len = dash_chunk(pkt, seq + i, sub, total, i + 1,
					body + i * DASH_CHUNK_DATA, part);

		g1_send(pkt, len);
		if (i + 1 < total) {
			expect_chunk_reply(pkt, 0x00);
		}
	}
}

/* ---- stocks ---- */

/* chunks 2..10 continue the vector's pattern */
static void send_stock_transfer(bool check_replies)
{
	static const char *const replies[11] = {
		"06 bd 00 d6 04 0b 00 01 00 00", "06 bd 00 d7 04 0b 00 02 00 00",
		"06 bd 00 d8 04 0b 00 03 00 00", "06 bd 00 d9 04 0b 00 04 00 00",
		"06 bd 00 da 04 0b 00 05 00 00", "06 bd 00 db 04 0b 00 06 00 00",
		"06 bd 00 dc 04 0b 00 07 00 00", "06 bd 00 dd 04 0b 00 08 00 00",
		"06 bd 00 de 04 0b 00 09 00 00", "06 bd 00 df 04 0b 00 0a 00 00",
		"06 99 00 e0 04 0b 00 0b 00 00",
	};
	uint8_t data[DASH_CHUNK_DATA];

	g1_send_vector("0x06_stock_chunk1");
	for (int k = 2; k <= 10; k++) {
		size_t len;

		fill_pattern(data, sizeof(data), 122 + (k - 2) * DASH_CHUNK_DATA);
		len = dash_chunk(pkt, 0xd6 + k - 1, DASH_SUB_STOCK, 11, k, data, sizeof(data));
		zassert_equal(len, 189);
		g1_send(pkt, len);
	}
	g1_send_vector("0x06_stock_last_chunk");
	if (check_replies) {
		for (int k = 0; k < 11; k++) {
			expect_frame(replies[k]);
		}
		expect_no_frame();
	} else {
		g1_frames_clear();
	}
}

ZTEST(dashboard, test_0x06_stock_eleven_chunks_are_acked_chunk_by_chunk)
{
	send_stock_transfer(true);
}

ZTEST(dashboard, test_0x06_stock_eleven_chunks_fill_item_1_with_the_largest_image)
{
	send_stock_transfer(false);

	fill_pattern(image, STOCK_IMAGE_MAX, 0);
	assert_stock(0, "GME.XNYS", STOCK_IMAGE_MAX);
	assert_stock_empty(1);
	zassert_equal(dts()->disp_mode, 0);
	zassert_equal(dts()->area, 1);
	zassert_equal(dts()->stocks_mode, 2);
}

ZTEST(dashboard, test_0x06_stock_one_packet_item_2_fills_its_fields)
{
	size_t n;

	memcpy(image, "\xde\xad\xbe\xef", 4);
	n = stock_body(2, "GME.XNYS", 4, 4);
	send_single(DASH_SUB_STOCK, 0x40, n);

	expect_chunk_reply(pkt, 0x00);
	expect_no_frame();
	assert_stock(1, "GME.XNYS", 4);
	assert_stock_empty(0);
}

ZTEST(dashboard, test_0x06_stock_item_4_is_the_last_slot)
{
	memcpy(image, "\x01\x02", 2);
	send_single(DASH_SUB_STOCK, 0x41, stock_body(4, "GME.XNYS", 2, 2));

	expect_chunk_reply(pkt, 0x00);
	assert_stock(3, "GME.XNYS", 2);
}

ZTEST(dashboard, test_0x06_stock_item_5_is_refused_with_status_1)
{
	memcpy(image, "\x01\x02", 2);
	send_single(DASH_SUB_STOCK, 0x42, stock_body(5, "GME.XNYS", 2, 2));

	expect_chunk_reply(pkt, 0x01);
	expect_no_frame();
	for (int i = 0; i < 4; i++) {
		assert_stock_empty(i);
	}
}

/* Defect, skipped (corrupts memory): item 0 writes stock_items[-1]. */
ZTEST(dashboard, test_0x06_stock_item_0_is_refused_with_status_1)
{
	ztest_test_skip();

	memcpy(image, "\x01\x02", 2);
	send_single(DASH_SUB_STOCK, 0x43, stock_body(0, "GME.XNYS", 2, 2));
	expect_chunk_reply(pkt, 0x01);
}

ZTEST(dashboard, test_0x06_stock_image_one_byte_over_the_bound_is_refused)
{
	/* the length check precedes any copy */
	send_single(DASH_SUB_STOCK, 0x44, stock_body(1, "GME.XNYS", STOCK_IMAGE_MAX + 1, 8));

	expect_chunk_reply(pkt, 0x01);
	expect_no_frame();
	zassert_equal(stock_items[0].valid, 0);
	zassert_equal(stock_items[0].image_len, 0);
}

/* Defect: the texts are copied before the image length check. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_stock_refused_item_keeps_the_previous_stock);
ZTEST(dashboard, test_0x06_stock_refused_item_keeps_the_previous_stock)
{
	memcpy(image, "\x01\x02", 2);
	send_single(DASH_SUB_STOCK, 0x45, stock_body(1, "OLD.XNYS", 2, 2));
	expect_chunk_reply(pkt, 0x00);

	send_single(DASH_SUB_STOCK, 0x46, stock_body(1, "GME.XNYS", STOCK_IMAGE_MAX + 1, 8));
	expect_chunk_reply(pkt, 0x01);

	assert_stock(0, "OLD.XNYS", 2);
}

/* Defect: an oversized field sends 01 and carries on, so a second reply follows. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_stock_oversize_code_gets_exactly_one_reply_with_status_1);
ZTEST(dashboard, test_0x06_stock_oversize_code_gets_exactly_one_reply_with_status_1)
{
	memcpy(image, "\x01\x02", 2);
	send_single(DASH_SUB_STOCK, 0x47, stock_body(1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456", 2, 2));

	zassert_equal(g1_frame_count(), 1, "%d replies", g1_frame_count());
	expect_chunk_reply(pkt, 0x01);
}

ZTEST(dashboard, test_0x06_stock_delete_action_clears_the_item)
{
	uint8_t del[5] = {0x00, 0x01, 0x02, 0x01, 0x02};

	memcpy(image, "\x01\x02", 2);
	send_single(DASH_SUB_STOCK, 0x48, stock_body(1, "GME.XNYS", 2, 2));
	expect_chunk_reply(pkt, 0x00);
	cleanStocksIndex_fake.call_count = 0;

	memcpy(body, del, sizeof(del));
	send_single(DASH_SUB_STOCK, 0x49, sizeof(del));

	expect_chunk_reply(pkt, 0x00);
	assert_stock_empty(0);
	zassert_equal(cleanStocksIndex_fake.call_count, 1);
}

ZTEST(dashboard, test_0x06_stock_count_and_index_are_16_bit)
{
	size_t len;

	memcpy(image, "\x01\x02", 2);
	len = dash_chunk(pkt, 0x4a, DASH_SUB_STOCK, 0x0101, 1, body, stock_body(1, "GME.XNYS", 2, 2));
	g1_send(pkt, len);

	expect_frame("06 45 00 4a 04 01 01 01 00 00");
	assert_stock_empty(0);
}

ZTEST(dashboard, test_0x06_stock_chunk_out_of_order_is_refused_with_status_1)
{
	uint8_t data[DASH_CHUNK_DATA];
	size_t len;

	fill_pattern(data, sizeof(data), 122);
	len = dash_chunk(pkt, 0xd7, DASH_SUB_STOCK, 11, 2, data, sizeof(data));
	g1_send(pkt, len);

	expect_frame("06 bd 00 d7 04 0b 00 02 00 01");
	assert_stock_empty(0);
}

/* ---- news ---- */

static const char cbs_text[] =
	"CBS Philadelphia's Jim Donovan announces retirement after more than 2 decades of service";

static void assert_news(int i, const char *source, const char *txt)
{
	struct news_item *nw = &news_items[i];

	zassert_equal(nw->valid, 1, "news %d not valid", i);
	zassert_true(strcmp(nw->source, source) == 0, "news %d source %.64s", i, nw->source);
	zassert_true(strcmp(nw->text, txt) == 0, "news %d text %.280s", i, nw->text);
}

static void assert_news_empty(int i)
{
	static const struct news_item zero;

	zassert_mem_equal(&news_items[i], &zero, sizeof(zero), "news %d not empty", i);
}

static size_t news_body(uint8_t num, const char *source, const char *txt, uint16_t txt_len)
{
	uint8_t *p = body;

	*p++ = 0x00;
	*p++ = 0x02;
	*p++ = 0x02;
	*p++ = num;
	*p++ = 0x01;
	p = put_tlv8(p, 1, source, strlen(source));
	p = put_tlv16(p, 2, txt, strlen(txt), txt_len);
	return p - body;
}

static const char *repeat(char c, size_t n)
{
	memset(text, c, n);
	text[n] = 0;
	return text;
}

ZTEST(dashboard, test_0x06_news_one_packet_item_is_acked_and_stored)
{
	g1_send_vector("0x06_news_item");

	expect_frame("06 73 00 52 05 01 00 01 00 00");
	expect_no_frame();
	assert_news(0, "CBS News", cbs_text);
	assert_news_empty(1);
	zassert_equal(dts()->news_mode, 2);
	zassert_equal(dts()->area, 2);
}

ZTEST(dashboard, test_0x06_news_item_5_is_refused_with_status_1)
{
	send_single(DASH_SUB_NEWS, 0x50, news_body(4, "Four", "fourth", 6));
	expect_chunk_reply(pkt, 0x00);
	assert_news(3, "Four", "fourth");

	send_single(DASH_SUB_NEWS, 0x51, news_body(5, "Five", "fifth", 5));
	expect_chunk_reply(pkt, 0x01);
	expect_no_frame();
	for (int i = 0; i < 3; i++) {
		assert_news_empty(i);
	}
	assert_news(3, "Four", "fourth");
}

ZTEST(dashboard, test_0x06_news_two_chunks_store_the_item)
{
	const char *txt = repeat('n', 250);

	send_chunked(DASH_SUB_NEWS, 0x52, news_body(2, "CBS News", txt, 250));

	expect_chunk_reply(pkt, 0x00);
	expect_no_frame();
	assert_news(1, "CBS News", repeat('n', 250));
}

ZTEST(dashboard, test_0x06_news_text_one_below_the_field_size_is_stored_whole)
{
	send_chunked(DASH_SUB_NEWS, 0x54, news_body(1, "CBS News", repeat('t', NEWS_TEXT_MAX - 1),
						     NEWS_TEXT_MAX - 1));

	expect_chunk_reply(pkt, 0x00);
	assert_news(0, "CBS News", repeat('t', NEWS_TEXT_MAX - 1));
}

/* Defect: 0x118 bytes are copied, leaving text[] without a NUL. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_news_text_of_the_field_size_is_not_stored_unterminated);
ZTEST(dashboard, test_0x06_news_text_of_the_field_size_is_not_stored_unterminated)
{
	send_chunked(DASH_SUB_NEWS, 0x56, news_body(1, "CBS News", repeat('t', NEWS_TEXT_MAX),
						     NEWS_TEXT_MAX));
	g1_frames_clear();

	zassert_equal(news_items[0].text[NEWS_TEXT_MAX - 1], 0, "text[0x117] is not a NUL");
}

ZTEST(dashboard, test_0x06_news_text_over_the_field_size_is_refused)
{
	send_chunked(DASH_SUB_NEWS, 0x58, news_body(1, "CBS News", repeat('t', NEWS_TEXT_MAX + 1),
						     NEWS_TEXT_MAX + 1));

	expect_chunk_reply(pkt, 0x01);
	expect_no_frame();
	zassert_equal(news_items[0].valid, 0);
	zassert_equal(news_items[0].text[0], 0);
}

ZTEST(dashboard, test_0x06_news_count_and_index_are_16_bit)
{
	int n = g1_vector_load("0x06_news_item", pkt, sizeof(pkt));

	zassert_equal(n, 115);
	pkt[6] = 0x01;
	g1_send(pkt, n);

	expect_frame("06 73 00 52 05 01 01 01 00 00");
	assert_news_empty(0);
}

ZTEST(dashboard, test_0x06_news_delete_action_clears_the_item)
{
	uint8_t del[5] = {0x00, 0x02, 0x02, 0x01, 0x02};

	g1_send_vector("0x06_news_item");
	expect_frame("06 73 00 52 05 01 00 01 00 00");

	memcpy(body, del, sizeof(del));
	send_single(DASH_SUB_NEWS, 0x5a, sizeof(del));

	expect_chunk_reply(pkt, 0x00);
	assert_news_empty(0);
}
