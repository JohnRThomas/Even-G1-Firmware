/*
 * CRC-32 (IEEE, table driven) and CRC-16 variants.
 */
#include <stddef.h>
#include "util/crc.h"

static uint32_t crc32_table[256];
void crc32_init(void)
{
	uint32_t i, j, c;

	for (i = 0; i < 256; i++) {
		c = i;
		for (j = 0; j < 8; j++) {
			if (c & 1) {
				c = (c >> 1) ^ 0xedb88320;
			} else {
				c >>= 1;
			}
		}
		crc32_table[i] = c;
	}
}

uint32_t crc32(uint32_t crc, const uint8_t *buf, int len)
{
	const uint8_t *end = buf + len;

	crc = ~crc;
	while (buf != end) {
		crc = crc32_table[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
	}
	return ~crc;
}

uint16_t crc16_usb(uint16_t crc, const uint8_t *buf, int len)
{
	if (buf == NULL || len <= 0) {
		return 0;
	}

	return crc16_generic(crc, 0x8005, 0xffff, 1, 1, buf, len);
}

/* generic CRC-16 (init, poly, xorout, reflect in / out) */
uint16_t crc16_generic(uint16_t crc, uint16_t poly, uint16_t xorout, int refin, bool refout,
		       const uint8_t *data, int len)
{
	uint16_t out;
	uint8_t b, r;
	int i, j;

	if (data == NULL) {
		return 0;
	}
	if (len <= 0) {
		return 0;
	}

	for (i = 0; i < len; i++) {
		b = data[i];
		if (refin) {
			r = 0;
			for (j = 7; j >= 0; j--) {
				if (b & 1) {
					r |= 1 << j;
				}
				b >>= 1;
			}
			b = r;
		}
		crc ^= b << 8;
		for (j = 0; j < 8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ poly;
			} else {
				crc <<= 1;
			}
		}
	}

	if (refout) {
		out = 0;
		for (j = 15; j >= 0; j--) {
			if (crc & 1) {
				out |= 1 << j;
			}
			crc >>= 1;
		}
		crc = out;
	}

	return xorout ^ crc;
}
