/*
 * Dashboard Set (0x06): 06 <len16> <seq> <sub> [<count16> <index16, 1-based>] <payload>.
 * Replies echo the header plus a status byte.
 */
#ifndef DASH_TEST_H_
#define DASH_TEST_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "ble/ble_proto.h"

#define DASH_SUB_TIME_WEATHER 1
#define DASH_SUB_CALENDAR     3
#define DASH_SUB_STOCK        4
#define DASH_SUB_NEWS         5
#define DASH_SUB_PANE_MODE    6
#define DASH_SUB_CANVAS       7

#define DASH_CHUNK_DATA 180

/* no zassert_str_equal() in Zephyr 3.4 */
#define assert_str(got, want)                                                                      \
	zassert_true(strcmp((got), (want)) == 0, "%s is \"%s\", expected \"%s\"", #got, (got),     \
		     (want))

static inline struct dashboard_ts_context *dts(void)
{
	return g1_state()->dashboard_ts;
}

size_t dash_chunk(uint8_t *buf, uint8_t seq, uint8_t sub, uint16_t total, uint16_t idx,
		  const void *payload, size_t n);

size_t dash_single(uint8_t *buf, uint8_t seq, uint8_t sub, const void *body, size_t n);

void expect_chunk_reply(const uint8_t *req, uint8_t status);

void expect_short_reply(const uint8_t *req, uint8_t status);

void fill_pattern(uint8_t *buf, size_t n, size_t start);

void expect_pattern(const uint8_t *buf, size_t n, size_t start, const char *what);

void expect_all(const uint8_t *buf, size_t n, uint8_t v, const char *what);

#endif /* DASH_TEST_H_ */
