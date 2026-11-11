#ifndef PROTO_DISPATCH_H_
#define PROTO_DISPATCH_H_

#include <stdint.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"

static inline void expect_frame_bytes(const uint8_t *want, size_t n)
{
	static char hex[3 * G1_FRAME_SIZE + 16];

	zassert_true(n <= G1_FRAME_SIZE, "test expects %u bytes", (unsigned int)n);
	g1_hex_format(want, n, hex, sizeof(hex));
	expect_frame(hex);
}

/* Unknown id in a handler's range: cmd, 0xCA, NUL-terminated message, zero-padded to 20. */
static inline void expect_error_reply(uint8_t cmd, const char *text)
{
	uint8_t want[20] = {0};

	zassert_true(strlen(text) + 1 <= sizeof(want) - 2, "text too long");
	want[0] = cmd;
	want[1] = 0xca;
	memcpy(&want[2], text, strlen(text) + 1);
	expect_frame_bytes(want, sizeof(want));
}

#endif /* PROTO_DISPATCH_H_ */
