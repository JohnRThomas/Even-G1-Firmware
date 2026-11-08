/*
 * Small helpers: UTC -> date conversion, weekday name, float formatting,
 * ANCS / hex dumps, uptime based timekeeping, generic CRC16 and a
 * run-length decoder.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "system/logging.h"
#include "util/common_utils.h"
#include "ui/gui/gui.h"
#include "ui/screens/ui.h"
#include "util/crc.h"

/* formats val with two decimals and a sign ("+1.23", "-0.05"); returns the length */
int format_float_2dp(float val, char *buf)
{
	int v = (int)(val * 100);
	uint32_t a = abs(v);
	char tmp[12] = {0};
	char *p = buf;
	int mode;
	int n = 0;
	int i;

	if (a < 10) {
		mode = 2;
	} else if (a < 100) {
		mode = 1;
	} else {
		mode = 0;
	}

	while (a) {
		if (n == 2 && mode == 0) {
			tmp[2] = '.';
			n = 3;
		}
		tmp[n++] = a % 10 + '0';
		a /= 10;
	}

	if (mode == 2) {
		tmp[n++] = '0';
	}
	*p = (v <= 0) ? '-' : '+';

	if (mode != 0) {
		p[1] = '0';
		p[2] = '.';
		p += 2;
		for (i = n - 1; i >= 0; i--) {
			*++p = tmp[i];
		}
		return n + 3;
	}

	for (i = 0; i < n; i++) {
		p[1 + i] = tmp[n - 1 - i];
	}
	p[n + 1] = '\0';
	return n + 1;
}

int uint32_to_little_endian(uint8_t *buf, uint32_t val)
{
	if (buf == NULL) {
		G1_LOG_WRN("buf is NULL !\n");
		return 0;
	}

	buf[0] = val;
	buf[1] = val >> 8;
	buf[2] = val >> 16;
	buf[3] = val >> 24;

	return 1;
}

/* (count, value) pair run-length decoder; returns the decoded length */
int rle_decode(const uint8_t *src, uint32_t len, uint8_t *dst)
{
	int out = 0;

	for (uint32_t i = 0; i < len; i += 2) {
		uint8_t cnt = src[i];

		memset(dst + out, src[i + 1], cnt);
		out += cnt;
	}

	return out;
}
