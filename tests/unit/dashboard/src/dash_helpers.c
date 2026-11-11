#include "dash_test.h"

size_t dash_chunk(uint8_t *buf, uint8_t seq, uint8_t sub, uint16_t total, uint16_t idx,
		  const void *payload, size_t n)
{
	size_t len = 9 + n;

	buf[0] = 0x06;
	buf[1] = len & 0xff;
	buf[2] = len >> 8;
	buf[3] = seq;
	buf[4] = sub;
	buf[5] = total & 0xff;
	buf[6] = total >> 8;
	buf[7] = idx & 0xff;
	buf[8] = idx >> 8;
	if (n) {
		memcpy(&buf[9], payload, n);
	}
	return len;
}

size_t dash_single(uint8_t *buf, uint8_t seq, uint8_t sub, const void *body, size_t n)
{
	size_t len = 5 + n;

	buf[0] = 0x06;
	buf[1] = len & 0xff;
	buf[2] = len >> 8;
	buf[3] = seq;
	buf[4] = sub;
	if (n) {
		memcpy(&buf[5], body, n);
	}
	return len;
}

void expect_chunk_reply(const uint8_t *req, uint8_t status)
{
	char hex[64];
	uint8_t want[10];

	memcpy(want, req, 9);
	want[9] = status;
	g1_hex_format(want, sizeof(want), hex, sizeof(hex));
	expect_frame(hex);
}

void expect_short_reply(const uint8_t *req, uint8_t status)
{
	char hex[32];
	uint8_t want[6];

	memcpy(want, req, 5);
	want[5] = status;
	g1_hex_format(want, sizeof(want), hex, sizeof(hex));
	expect_frame(hex);
}

void fill_pattern(uint8_t *buf, size_t n, size_t start)
{
	for (size_t i = 0; i < n; i++) {
		buf[i] = (start + i) & 0xff;
	}
}

void expect_pattern(const uint8_t *buf, size_t n, size_t start, const char *what)
{
	for (size_t i = 0; i < n; i++) {
		zassert_equal(buf[i], (start + i) & 0xff, "%s byte %u is 0x%02x, expected 0x%02x",
			      what, (unsigned int)i, buf[i], (unsigned int)((start + i) & 0xff));
	}
}

void expect_all(const uint8_t *buf, size_t n, uint8_t v, const char *what)
{
	for (size_t i = 0; i < n; i++) {
		zassert_equal(buf[i], v, "%s byte %u is 0x%02x, expected 0x%02x", what,
			      (unsigned int)i, buf[i], v);
	}
}
