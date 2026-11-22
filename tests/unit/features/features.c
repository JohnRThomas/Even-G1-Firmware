#include "features.h"

static uint8_t guard_area[0x2000];

static struct {
  void **slot;
  void *orig;
  size_t front;
  size_t size;
  size_t back;
} guard;

void feat_guard_block(void *slot, size_t size, size_t front, size_t back) {
  zassert_is_null(guard.slot, "one guarded block per test");
  zassert_true(front + size + back <= sizeof(guard_area), "guard area too small");
  memset(guard_area, FEAT_GUARD_FILL, front + size + back);
  memset(guard_area + front, 0, size);
  guard.slot = slot;
  guard.orig = *guard.slot;
  guard.front = front;
  guard.size = size;
  guard.back = back;
  *guard.slot = guard_area + front;
}

void feat_expect_guard_intact(void) {
  zassert_not_null(guard.slot, "no guarded block");
  expect_fill(guard_area, guard.front, FEAT_GUARD_FILL, "guard before the block:");
  expect_fill(guard_area + guard.front + guard.size, guard.back, FEAT_GUARD_FILL, "guard past the block end:");
}

static void features_after(void *fixture) {
  ARG_UNUSED(fixture);
  if (guard.slot != NULL) {
    *guard.slot = guard.orig;
    guard.slot = NULL;
  }
}

ZTEST_SUITE(features, NULL, NULL, NULL, features_after, NULL);

void feat_expect_field(const void *field, size_t cap, const char *str) {
  const char *chars = field;
  size_t len = strlen(str);
  char what[96];

  zassert_true(len < cap, "test string longer than the field");
  zassert_mem_equal(chars, str, len, "field holds \"%.*s\", want \"%s\"", (int)cap, chars, str);
  snprintf(what, sizeof(what), "field after \"%s\":", str);
  expect_fill(&chars[len], cap - len, 0, what);
}

void feat_expect_queued_count(int count) {
  zassert_equal(g1_esb_response_log.count, count, "%d replies queued for the slave path, want %d",
                g1_esb_response_log.count, count);
}

size_t feat_set_len16(uint8_t *pkt, size_t len) {
  sys_put_le16(len, &pkt[1]);
  return len;
}
