#ifndef PROTO_SESSION_H_
#define PROTO_SESSION_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "harness.h"

static inline void expect_frame_bytes(const uint8_t *want, size_t n)
{
	static char hex[3 * G1_FRAME_SIZE + 16];

	zassert_true(n <= G1_FRAME_SIZE, "test expects %u bytes", (unsigned int)n);
	g1_hex_format(want, n, hex, sizeof(hex));
	expect_frame(hex);
}

/* byte off replaced by val, e.g. the sequence byte */
static inline void send_vector_with(const char *name, int off, uint8_t val)
{
	uint8_t buf[256];
	int n = g1_vector_load(name, buf, sizeof(buf));

	zassert_true(n > off, "vector %s: %d bytes, patching byte %d", name, n, off);
	buf[off] = val;
	g1_send(buf, n);
}

/* the request was handed to the dashboard thread */
static inline bool dashboard_notified(void)
{
	return k_sem_count_get(&g1_state()->dashboard_position_sem) == 1;
}

#endif /* PROTO_SESSION_H_ */
