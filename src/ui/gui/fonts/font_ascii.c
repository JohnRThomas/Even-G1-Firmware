/*
 * ASCII glyph helpers of the font code; the CJK glyphs are in font_bitmap.c.
 */
#include <stdint.h>
#include <stddef.h>

#include "ui/gui/fonts/font_bitmap.h"

/* ASCII glyphs are drawn elsewhere */
const uint8_t *font_ascii_lookup(uint8_t c, int size)
{
	return NULL;
}

/*
 * ASCII glyph of a w x h font: there is no ASCII table, so the result is the
 * glyph offset from a NULL base (arg is unused).
 */
const uint8_t *font_ascii_get_glyph(uint16_t digit, int w, int h, int arg)
{
	if (digit >= 0x20 && digit < 0x80) {
		/* NOLINTNEXTLINE(performance-no-int-to-ptr) */
		return (const uint8_t *)((digit - 0x20) * (w * h));
	}
	return NULL;
}

int check_printable_code(uint32_t code)
{
	return code > 0x1f ? 0 : -1;
}
