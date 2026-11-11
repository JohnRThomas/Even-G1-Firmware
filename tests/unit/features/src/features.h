#ifndef FEATURES_H_
#define FEATURES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "harness.h"

void feat_expect_bytes(const uint8_t *buf, size_t len, const char *hex);

/* str, NUL-padded to cap */
void feat_expect_field(const char *field, size_t cap, const char *str);

/* replies queued for the phone */
void feat_expect_queued(int i, const char *hex);
void feat_expect_queued_count(int n);

size_t feat_vector(const char *name, uint8_t *buf, size_t cap);

/* u16 LE at pkt[1..2] */
size_t feat_set_len16(uint8_t *pkt, size_t len);

/* Points *slot at a zeroed block inside FEAT_GUARD_FILL guards; undone after each test. */
#define FEAT_GUARD_FILL 0xa5
void *feat_guard_block(void *slot, size_t size, size_t front, size_t back);
void feat_expect_guard_intact(void);

#endif /* FEATURES_H_ */
