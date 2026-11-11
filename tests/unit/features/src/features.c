/* Not reset between tests: file_storage.c's upload position and quicknote_cache. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_esb.h"
#include "features.h"

static uint8_t guard_area[0x2000];

static struct {
	void **slot;
	void *orig;
	size_t front;
	size_t size;
	size_t back;
} guard;

void *feat_guard_block(void *slot, size_t size, size_t front, size_t back)
{
	void **s = slot;

	zassert_is_null(guard.slot, "one guarded block per test");
	zassert_true(front + size + back <= sizeof(guard_area), "guard area too small");
	memset(guard_area, FEAT_GUARD_FILL, front + size + back);
	memset(guard_area + front, 0, size);
	guard.slot = s;
	guard.orig = *s;
	guard.front = front;
	guard.size = size;
	guard.back = back;
	*s = guard_area + front;
	return *s;
}

void feat_expect_guard_intact(void)
{
	zassert_not_null(guard.slot, "no guarded block");
	for (size_t i = 0; i < guard.front; i++) {
		zassert_equal(guard_area[i], FEAT_GUARD_FILL,
			      "written %u bytes before the block (0x%02x)",
			      (unsigned int)(guard.front - i), guard_area[i]);
	}
	for (size_t i = 0; i < guard.back; i++) {
		size_t at = guard.front + guard.size + i;

		zassert_equal(guard_area[at], FEAT_GUARD_FILL,
			      "written at block + 0x%x, %u bytes past its end (0x%02x)",
			      (unsigned int)(guard.size + i), (unsigned int)i, guard_area[at]);
	}
}

static void features_after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (guard.slot != NULL) {
		*guard.slot = guard.orig;
		guard.slot = NULL;
	}
}

ZTEST_SUITE(features, NULL, NULL, NULL, features_after, NULL);

void feat_expect_bytes(const uint8_t *buf, size_t len, const char *hex)
{
	static uint8_t want[512];
	static char got_txt[1600];
	static char want_txt[1600];
	int n = g1_hex_parse(hex, want, sizeof(want));

	zassert_true(n >= 0, "bad hex in test: %s", hex);
	g1_hex_format(buf, len, got_txt, sizeof(got_txt));
	g1_hex_format(want, n, want_txt, sizeof(want_txt));
	zassert_equal(len, (size_t)n, "length %u, want %d\n want %s\n  got %s",
		      (unsigned int)len, n, want_txt, got_txt);
	zassert_mem_equal(buf, want, len, "\n want %s\n  got %s", want_txt, got_txt);
}

void feat_expect_field(const char *field, size_t cap, const char *str)
{
	size_t n = strlen(str);

	zassert_true(n < cap, "test string longer than the field");
	zassert_mem_equal(field, str, n, "field holds \"%.*s\", want \"%s\"", (int)cap, field,
			  str);
	for (size_t i = n; i < cap; i++) {
		zassert_equal(field[i], 0, "field[%u] = 0x%02x after \"%s\"", (unsigned int)i,
			      (uint8_t)field[i], str);
	}
}

void feat_expect_queued(int i, const char *hex)
{
	zassert_true(i < g1_esb_response_log.count, "%d replies queued for the slave path, want #%d",
		     g1_esb_response_log.count, i);
	feat_expect_bytes(g1_esb_response_log.data[i], g1_esb_response_log.len[i], hex);
}

void feat_expect_queued_count(int n)
{
	zassert_equal(g1_esb_response_log.count, n, "%d replies queued for the slave path, want %d",
		      g1_esb_response_log.count, n);
}

size_t feat_vector(const char *name, uint8_t *buf, size_t cap)
{
	int n = g1_vector_load(name, buf, cap);

	zassert_true(n > 0, "vector %s: %d", name, n);
	return (size_t)n;
}

size_t feat_set_len16(uint8_t *pkt, size_t len)
{
	pkt[1] = (uint8_t)len;
	pkt[2] = (uint8_t)(len >> 8);
	return len;
}
