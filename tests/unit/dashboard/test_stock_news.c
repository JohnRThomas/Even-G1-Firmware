/*
 * Dashboard Set sub 4 (stocks) and sub 5 (news). Item body: <dash mode> <pane> 02 <item 1..4>
 * <action 01 set / 02 delete> <TLVs>.
 */
#include "dash_test.h"

#define STOCK_IMAGE_MAX 0x75e
#define NEWS_TEXT_MAX   0x118

static uint8_t pkt[256];
static uint8_t body[0x900];
static uint8_t image[0x800];
static char text[0x200];

/* <type> <declared_len16> and len bytes of value */
static uint8_t *put_tlv16(uint8_t *pos, uint8_t type, const void *value, size_t len, uint16_t declared_len) {
  *pos++ = type;
  sys_put_le16(declared_len, pos);
  memcpy(pos + 2, value, len);
  return pos + 2 + len;
}

/* the item header with dash mode 00 at the start of body[]; returns its length */
static size_t put_item_header(uint8_t pane, uint8_t item, uint8_t action) {
  const uint8_t header[] = {0x00, pane, 0x02, item, action};

  memcpy(body, header, sizeof(header));
  return sizeof(header);
}

/* sets item; image_len is declared, image_sent bytes 01 02 03 ... (kept in image[]) are sent */
static size_t stock_body(uint8_t item, const char *code, uint16_t image_len, size_t image_sent) {
  uint8_t *pos = body + put_item_header(0x01, item, 0x01);

  pos = put_tlv8(pos, 1, code, strlen(code));
  pos = put_tlv8(pos, 2, "GameStop Corp", 13);
  pos = put_tlv8(pos, 3, "21.36", 5);
  pos = put_tlv8(pos, 4, "-0.09", 5);
  pos = put_tlv8(pos, 5, "-0.42%", 6);
  pos = put_tlv8(pos, 6, "\x02", 1);
  g1_fill_pattern(image, image_sent, 1);
  pos = put_tlv16(pos, 7, image, image_sent, image_len);
  return pos - body;
}

static void assert_stock(int slot, const char *code, uint16_t image_len) {
  struct stock_item *item = &stock_items[slot];

  zassert_equal(item->valid, 1, "stock %d not valid", slot);
  zassert_true(strcmp(item->code, code) == 0, "stock %d code %.32s", slot, item->code);
  zassert_true(strcmp(item->company, "GameStop Corp") == 0, "company %.64s", item->company);
  zassert_true(strcmp(item->price, "21.36") == 0, "price %.16s", item->price);
  zassert_true(strcmp(item->price_change, "-0.09") == 0, "change %.16s", item->price_change);
  zassert_true(strcmp(item->change_rate, "-0.42%") == 0, "rate %.16s", item->change_rate);
  zassert_equal(item->change_status, 2);
  zassert_equal(item->image_len, image_len, "image_len %u", item->image_len);
  zassert_mem_equal(item->image, image, image_len);
}

static void assert_stock_empty(int slot) {
  static const struct stock_item zero;

  zassert_mem_equal(&stock_items[slot], &zero, sizeof(zero), "stock %d not empty", slot);
}

static void send_single(uint8_t sub, uint8_t seq, size_t len) {
  size_t pkt_len = g1_put_chunk(pkt, 0x06, seq, sub, 1, 1, body, len);

  zassert_true(pkt_len <= sizeof(pkt));
  g1_send(pkt, pkt_len);
}

/* ---- stocks ---- */

/* chunks 2..10 continue the vector's pattern */
static void send_stock_transfer(void) {
  uint8_t data[DASH_CHUNK_DATA];

  g1_send_vector("0x06_stock_chunk1");
  for (int chunk = 2; chunk <= 10; chunk++) {
    size_t len;

    g1_fill_pattern(data, sizeof(data), 122 + (chunk - 2) * DASH_CHUNK_DATA);
    len = g1_put_chunk(pkt, 0x06, 0xd5 + chunk, DASH_SUB_STOCK, 11, chunk, data, sizeof(data));
    zassert_equal(len, 189);
    g1_send(pkt, len);
  }
  g1_send_vector("0x06_stock_last_chunk");
}

ZTEST(dashboard, test_0x06_stock_eleven_chunks_are_acked_chunk_by_chunk) {
  char hex[32];

  send_stock_transfer();

  for (int chunk = 1; chunk <= 10; chunk++) {
    snprintf(hex, sizeof(hex), "06 bd 00 %02x 04 0b 00 %02x 00 00", 0xd5 + chunk, chunk);
    expect_frame(hex);
  }
  expect_only_frame("06 99 00 e0 04 0b 00 0b 00 00");
}

ZTEST(dashboard, test_0x06_stock_eleven_chunks_fill_item_1_with_the_largest_image) {
  send_stock_transfer();
  g1_frames_clear();

  g1_fill_pattern(image, STOCK_IMAGE_MAX, 0);
  assert_stock(0, "GME.XNYS", STOCK_IMAGE_MAX);
  assert_stock_empty(1);
  zassert_equal(dts()->disp_mode, 0);
  zassert_equal(dts()->area, 1);
  zassert_equal(dts()->stocks_mode, 2);
}

ZTEST(dashboard, test_0x06_stock_one_packet_item_2_fills_its_fields) {
  send_single(DASH_SUB_STOCK, 0x40, stock_body(2, "GME.XNYS", 4, 4));

  expect_chunk_reply(pkt, 0x00);
  expect_no_frame();
  assert_stock(1, "GME.XNYS", 4);
  assert_stock_empty(0);
}

ZTEST(dashboard, test_0x06_stock_item_4_is_the_last_slot) {
  send_single(DASH_SUB_STOCK, 0x41, stock_body(4, "GME.XNYS", 2, 2));

  expect_chunk_reply(pkt, 0x00);
  assert_stock(3, "GME.XNYS", 2);
}

ZTEST(dashboard, test_0x06_stock_item_5_is_refused_with_status_1) {
  send_single(DASH_SUB_STOCK, 0x42, stock_body(5, "GME.XNYS", 2, 2));

  expect_chunk_reply(pkt, 0x01);
  expect_no_frame();
  for (int slot = 0; slot < 4; slot++) {
    assert_stock_empty(slot);
  }
}

ZTEST(dashboard, test_0x06_stock_item_0_is_refused_with_status_1) {
  send_single(DASH_SUB_STOCK, 0x43, stock_body(0, "GME.XNYS", 2, 2));
  expect_chunk_reply(pkt, 0x01);
}

ZTEST(dashboard, test_0x06_stock_image_one_byte_over_the_bound_is_refused) {
  /* the length check precedes any copy */
  send_single(DASH_SUB_STOCK, 0x44, stock_body(1, "GME.XNYS", STOCK_IMAGE_MAX + 1, 8));

  expect_chunk_reply(pkt, 0x01);
  expect_no_frame();
  zassert_equal(stock_items[0].valid, 0);
  zassert_equal(stock_items[0].image_len, 0);
}

ZTEST(dashboard, test_0x06_stock_refused_item_keeps_the_previous_stock) {
  send_single(DASH_SUB_STOCK, 0x45, stock_body(1, "OLD.XNYS", 2, 2));
  expect_chunk_reply(pkt, 0x00);

  send_single(DASH_SUB_STOCK, 0x46, stock_body(1, "GME.XNYS", STOCK_IMAGE_MAX + 1, 8));
  expect_chunk_reply(pkt, 0x01);

  assert_stock(0, "OLD.XNYS", 2);
}

ZTEST(dashboard, test_0x06_stock_oversize_code_gets_exactly_one_reply_with_status_1) {
  send_single(DASH_SUB_STOCK, 0x47, stock_body(1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456", 2, 2));

  zassert_equal(g1_frame_count(), 1, "%d replies", g1_frame_count());
  expect_chunk_reply(pkt, 0x01);
}

ZTEST(dashboard, test_0x06_stock_delete_action_clears_the_item) {
  send_single(DASH_SUB_STOCK, 0x48, stock_body(1, "GME.XNYS", 2, 2));
  expect_chunk_reply(pkt, 0x00);
  cleanStocksIndex_fake.call_count = 0;

  send_single(DASH_SUB_STOCK, 0x49, put_item_header(0x01, 1, 0x02)); /* delete item 1 */

  expect_chunk_reply(pkt, 0x00);
  assert_stock_empty(0);
  zassert_equal(cleanStocksIndex_fake.call_count, 1);
}

ZTEST(dashboard, test_0x06_stock_count_and_index_are_16_bit) {
  g1_send(pkt, g1_put_chunk(pkt, 0x06, 0x4a, DASH_SUB_STOCK, 0x0101, 1, body, stock_body(1, "GME.XNYS", 2, 2)));

  expect_frame("06 45 00 4a 04 01 01 01 00 00");
  assert_stock_empty(0);
}

ZTEST(dashboard, test_0x06_stock_chunk_out_of_order_is_refused_with_status_1) {
  uint8_t data[DASH_CHUNK_DATA];

  g1_fill_pattern(data, sizeof(data), 122);
  g1_send(pkt, g1_put_chunk(pkt, 0x06, 0xd7, DASH_SUB_STOCK, 11, 2, data, sizeof(data)));

  expect_frame("06 bd 00 d7 04 0b 00 02 00 01");
  assert_stock_empty(0);
}

/* ---- news ---- */

/* sets item: 01 <len> source, 02 <len16> text */
static size_t news_body(uint8_t item, const char *source, const char *news_text) {
  uint8_t *pos = body + put_item_header(0x02, item, 0x01);

  pos = put_tlv8(pos, 1, source, strlen(source));
  pos = put_tlv16(pos, 2, news_text, strlen(news_text), strlen(news_text));
  return pos - body;
}

/* 180-byte chunks; the last chunk's reply stays queued */
static void send_news_chunked(uint8_t seq, size_t len) {
  uint16_t total = DIV_ROUND_UP(len, DASH_CHUNK_DATA);

  for (uint16_t index = 0; index < total; index++) {
    size_t part = MIN(DASH_CHUNK_DATA, len - index * DASH_CHUNK_DATA);

    g1_send(
      pkt, g1_put_chunk(pkt, 0x06, seq + index, DASH_SUB_NEWS, total, index + 1, body + index * DASH_CHUNK_DATA, part));
    if (index + 1 < total) {
      expect_chunk_reply(pkt, 0x00);
    }
  }
}

static void assert_news(int slot, const char *source, const char *news_text) {
  struct news_item *item = &news_items[slot];

  zassert_equal(item->valid, 1, "news %d not valid", slot);
  zassert_true(strcmp(item->source, source) == 0, "news %d source %.64s", slot, item->source);
  zassert_true(strcmp(item->text, news_text) == 0, "news %d text %.280s", slot, item->text);
}

static void assert_news_empty(int slot) {
  static const struct news_item zero;

  zassert_mem_equal(&news_items[slot], &zero, sizeof(zero), "news %d not empty", slot);
}

ZTEST(dashboard, test_0x06_news_one_packet_item_is_acked_and_stored) {
  g1_send_vector("0x06_news_item");

  expect_only_frame("06 73 00 52 05 01 00 01 00 00");
  assert_news(0, "CBS News",
              "CBS Philadelphia's Jim Donovan announces retirement after more "
              "than 2 decades of service");
  assert_news_empty(1);
  zassert_equal(dts()->news_mode, 2);
  zassert_equal(dts()->area, 2);
}

ZTEST(dashboard, test_0x06_news_item_5_is_refused_with_status_1) {
  send_single(DASH_SUB_NEWS, 0x50, news_body(4, "Four", "fourth"));
  expect_chunk_reply(pkt, 0x00);
  assert_news(3, "Four", "fourth");

  send_single(DASH_SUB_NEWS, 0x51, news_body(5, "Five", "fifth"));
  expect_chunk_reply(pkt, 0x01);
  expect_no_frame();
  for (int slot = 0; slot < 3; slot++) {
    assert_news_empty(slot);
  }
  assert_news(3, "Four", "fourth");
}

ZTEST(dashboard, test_0x06_news_two_chunks_store_the_item) {
  send_news_chunked(0x52, news_body(2, "CBS News", repeat(text, 'n', 250)));

  expect_chunk_reply(pkt, 0x00);
  expect_no_frame();
  assert_news(1, "CBS News", repeat(text, 'n', 250));
}

ZTEST(dashboard, test_0x06_news_text_one_below_the_field_size_is_stored_whole) {
  send_news_chunked(0x54, news_body(1, "CBS News", repeat(text, 't', NEWS_TEXT_MAX - 1)));

  expect_chunk_reply(pkt, 0x00);
  assert_news(0, "CBS News", repeat(text, 't', NEWS_TEXT_MAX - 1));
}

ZTEST(dashboard, test_0x06_news_text_of_the_field_size_is_not_stored_unterminated) {
  send_news_chunked(0x56, news_body(1, "CBS News", repeat(text, 't', NEWS_TEXT_MAX)));
  g1_frames_clear();

  zassert_equal(news_items[0].text[NEWS_TEXT_MAX - 1], 0, "text[0x117] is not a NUL");
}

ZTEST(dashboard, test_0x06_news_text_over_the_field_size_is_refused) {
  send_news_chunked(0x58, news_body(1, "CBS News", repeat(text, 't', NEWS_TEXT_MAX + 1)));

  expect_chunk_reply(pkt, 0x01);
  expect_no_frame();
  zassert_equal(news_items[0].valid, 0);
  zassert_equal(news_items[0].text[0], 0);
}

ZTEST(dashboard, test_0x06_news_count_and_index_are_16_bit) {
  zassert_equal(g1_send_vector_with("0x06_news_item", 6, 0x01), 115);

  expect_frame("06 73 00 52 05 01 01 01 00 00");
  assert_news_empty(0);
}

ZTEST(dashboard, test_0x06_news_delete_action_clears_the_item) {
  g1_send_vector("0x06_news_item");
  expect_frame("06 73 00 52 05 01 00 01 00 00");

  send_single(DASH_SUB_NEWS, 0x5a, put_item_header(0x02, 1, 0x02)); /* delete item 1 */

  expect_chunk_reply(pkt, 0x00);
  assert_news_empty(0);
}

/* ---- chunk buffers ---- */

/*
 * A pane collects its chunks in a buffer (calendar 0x538, stock 0x812, news 0x161 bytes): the
 * chunk that no longer fits is refused, and the transfer with it.
 */
ZTEST(dashboard, test_0x06_chunk_past_the_end_of_the_pane_buffer_is_refused_with_status_1) {
  static const struct {
    uint8_t sub;
    size_t size;
  } rows[] = {
    {DASH_SUB_CALENDAR, 0x538},
    {DASH_SUB_STOCK, 0x812},
    {DASH_SUB_NEWS, 0x161},
  };
  uint8_t data[DASH_CHUNK_DATA] = {0};

  for (unsigned int row = 0; row < ARRAY_SIZE(rows); row++) {
    uint16_t fit = rows[row].size / DASH_CHUNK_DATA;

    /* the one past the end, and the next one, which has nothing to continue */
    for (uint16_t chunk = 1; chunk <= fit + 2; chunk++) {
      g1_send(pkt, g1_put_chunk(pkt, 0x06, chunk, rows[row].sub, 100, chunk, data, sizeof(data)));
      expect_chunk_reply(pkt, chunk <= fit ? 0x00 : 0x01);
    }
  }
  expect_no_frame();
}
