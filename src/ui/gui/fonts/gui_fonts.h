/*
 * Glyph tables of gui_fonts.c (4 bpp bitmaps and their code point indexes).
 */
#ifndef G1_UI_GUI_FONTS_GUI_FONTS_H_
#define G1_UI_GUI_FONTS_GUI_FONTS_H_

#include <stdint.h>

struct font_glyph_idx {
	uint16_t code;
	uint8_t width;      /* in bytes (2 pixels per byte) */
};
extern const int font26_glyph_count;
extern const int font_b_glyph_count;
extern const int font_c_glyph_count;
extern const int font39_glyph_count;
extern const struct font_glyph_idx font26_index[];
extern const struct font_glyph_idx font_b_index[];
extern const struct font_glyph_idx font_c_index[];
extern const struct font_glyph_idx font39_index[];
extern const uint8_t font26_bitmap[];
extern const uint8_t font_b_bitmap[];
extern const uint8_t font_c_bitmap[];
extern const uint8_t font39_bitmap[];

#endif /* G1_UI_GUI_FONTS_GUI_FONTS_H_ */
