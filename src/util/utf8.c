/*
 * UTF-8 decoding into 16-bit code points.
 */
#include <stdio.h>
#include <string.h>
#include "util/utf8.h"

static uint16_t unicode_buf[0x400];
/* decode one 2 or 3 byte UTF-8 sequence into a unicode code point */
uint16_t utf8_decode_char(const uint8_t *s, int len)
{
	uint8_t c = s[0];
	uint8_t hi, lo;
	uint8_t top2, top4;

	if ((c & 0xf0) == 0xe0) {
		if (len < 3) {
			return 0;
		}
		top2 = s[1] & 0x03;
		top4 = c & 0x0f;
		lo = (((s[2] >> 4) & 0x03) << 4) | (top2 << 6) | (s[2] & 0x0f);
		hi = ((s[1] >> 2) & 0x0f) | (top4 << 4);
	} else if ((c & 0xe0) == 0xc0) {
		if (len < 2) {
			printf("length error!\n");
			return 0;
		}
		top2 = c & 0x03;
		lo = (s[1] & 0x3f) | (top2 << 6);
		hi = (c & 0x1f) >> 2;
	} else {
		printf("maybe [%x] not unicode ?\n", c);
		return 0;
	}
	return lo | (hi << 8);
}

/* UTF-8 string -> unicode array (ASCII, CR/LF, 2 and 3 byte sequences) */
int utf8_to_unicode(const char *utf8, int len, uint16_t *out, int max)
{
	int k = 0;
	int i = 0;

	while (i < len) {
		uint8_t c = utf8[i];
		uint16_t u;

		if (c == 0) {
			break;
		}
		if (c == '\r' || c == '\n' || (c >= 0x20 && c < 0x80)) {
			u = c;
			i++;
		} else if ((c & 0xf0) == 0xe0) {
			u = utf8_decode_char((const uint8_t *)&utf8[i], 3);
			i += 3;
		} else if ((c & 0xe0) == 0xc0) {
			u = utf8_decode_char((const uint8_t *)&utf8[i], 2);
			i += 2;
		} else {
			i++;
			continue;
		}
		out[k] = u;
		if (k == max - 1) {
			printf("maybe array overflow!(k=%d)\n", k);
			break;
		}
		k++;
	}
	out[k] = 0;
	return k;
}

uint16_t *utf8_to_unicode_buf(const char *utf8, uint16_t *count)
{
	memset(unicode_buf, 0, sizeof(unicode_buf));
	*count = utf8_to_unicode(utf8, strlen(utf8), unicode_buf, 0x400);
	return unicode_buf;
}
