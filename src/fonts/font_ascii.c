/*
 * ASCII glyph helpers of the font code; the CJK glyphs are in font_bitmap.c.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "../gui/gui.h"
#include "font_bitmap.h"

/*
 * ASCII glyph in a cell of w bytes x h rows (4 bpp, as the CJK glyphs), drawn into cell: the
 * 26-row font B glyph at the left, centred vertically. A code without a glyph leaves the cell empty.
 */
const uint8_t *font_ascii_lookup(uint8_t c, int w, int h, uint8_t *cell) {
  int glyph_w = 0;
  int glyph_h = 0;
  const uint8_t *glyph = font_b_lookup(c, &glyph_w, &glyph_h);
  int top;

  memset(cell, 0, w * h);
  if (glyph == NULL) {
    return cell;
  }
  /* font_b_lookup() gives the width in pixels, two to a byte */
  glyph_w /= 2;
  top = MAX(h - glyph_h, 0) / 2;
  for (int row = 0; row < MIN(glyph_h, h); row++) {
    memcpy(cell + (top + row) * w, glyph + row * glyph_w, MIN(glyph_w, w));
  }
  return cell;
}

int check_printable_code(uint32_t code) {
  return code > 0x1f ? 0 : -1;
}
