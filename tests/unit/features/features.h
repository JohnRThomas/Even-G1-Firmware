#ifndef FEATURES_H_
#define FEATURES_H_

#include "fakes.h"
#include "ble/ble_proto.h"

/* str, NUL-padded to cap */
void feat_expect_field(const void *field, size_t cap, const char *str);

/* replies queued for the phone */
void feat_expect_queued_count(int count);

/* u16 LE at pkt[1..2] */
size_t feat_set_len16(uint8_t *pkt, size_t len);

/* Points *slot at a zeroed block inside FEAT_GUARD_FILL guards; undone after each test. */
#define FEAT_GUARD_FILL 0xa5
void feat_guard_block(void *slot, size_t size, size_t front, size_t back);
void feat_expect_guard_intact(void);

#endif /* FEATURES_H_ */
