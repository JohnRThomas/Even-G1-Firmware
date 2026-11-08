/* Glyph bitmaps (font_bitmap.c): CJK and small glyph lookup, 1 bpp to 4 bpp expansion */
#ifndef PD_FONT_BITMAP_H_
#define PD_FONT_BITMAP_H_

#include <stdint.h>
#include "system/glasses_state.h"

void expand_1bpp_to_4bpp(const uint8_t *src, int len, uint8_t *dst);
const uint8_t *get_small_glyph_bitmap(int glyph, int size);
const uint8_t *find_chinese_bitmap_by_unicode(int code, int w, int h);
const uint8_t *font_ascii_lookup(uint8_t c, int size);
const uint8_t *font_ascii_get_glyph(uint16_t digit, int w, int h, int arg);
int check_printable_code(uint32_t code);

/* font_bitmap_data.inc: 6x5 pixel markers of the demo position page (4 bpp) */
extern const uint8_t demo_mark_left[15];
extern const uint8_t demo_mark_right[15];

#endif
