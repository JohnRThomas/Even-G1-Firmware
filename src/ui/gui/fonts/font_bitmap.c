/*
 * Font bitmap helpers: UTF-8 string -> unicode buffer, 1 bpp -> 4 bpp glyph
 * expansion, CJK glyph lookup in the external flash font area.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "system/logging.h"
#include "ui/gui/fonts/font_bitmap.h"
#include "ui/screens/demo_image.h"

/* glyph index entry in the external flash (0x140000 + (code - 0xa4) * 4) */
struct cjk_glyph_index {
	uint16_t unicode;
	uint16_t offset;
};

/* every source byte (8 pixels, 1 bpp) maps to 4 bytes (8 pixels, 4 bpp) */
#include "ui/gui/fonts/font_bitmap_data.inc"
#include "ui/gui/fonts/font_1bpp_to_4bpp.inc"

static uint8_t small_glyph_buf[0x120];
static uint8_t cjk_glyph_buf[0x1b0];

void expand_1bpp_to_4bpp(const uint8_t *src, int len, uint8_t *dst)
{
	for (int i = 0; i < len; i++) {
		((uint32_t *)dst)[i] = ((const uint32_t *)font_1bpp_to_4bpp)[src[i]];
	}
}

const uint8_t *find_chinese_bitmap_by_unicode(int code, int w, int h)
{
	uint16_t size = w * h;
	uint8_t buf[size];
	uint8_t *bitmap = buf;
	const uint8_t *ret;
	struct cjk_glyph_index idx;
	uint32_t addr;
	int err;

	if (code >= 0x20 && code < 0x80) {
		ret = font_ascii_lookup(code, size);
	} else {
		if (get_dashboard_state()->flash_read == NULL) {
			bitmap = NULL;
		} else {
			if (get_dashboard_state()->flash_read(get_dashboard_state(),
							      ((code - 0xa4) << 2) + 0x140000, &idx,
							      sizeof(idx))) {
				G1_LOG_ERR("read flash fail\n\n");
				return NULL;
			}
			printk("unicode=%x, offset=%d\n", idx.unicode, idx.offset);
			addr = (size / 4) * idx.offset;
			if (size == 0x1b0) {
				addr += 0xcf120;
			}
			memset(buf, 0, size);
			err = get_dashboard_state()->flash_read(get_dashboard_state(),
								addr + 0x200000, buf, size / 4);
			if (err) {
				G1_LOG_ERR("read flash fail: %d\n\n", err);
			}
		}
		expand_1bpp_to_4bpp(bitmap, size, cjk_glyph_buf);
		ret = cjk_glyph_buf;
	}
	return ret;
}

const uint8_t *get_small_glyph_bitmap(int glyph, int size)
{
	if (glyph == 4) {
		memcpy(small_glyph_buf, font_glyph4_4bpp, sizeof(small_glyph_buf));
	} else {
		int n = size / 4;

		expand_1bpp_to_4bpp(&font_small_glyphs[n * glyph], n, small_glyph_buf);
	}
	return small_glyph_buf;
}
