#ifndef G1_UNIT_GUI_TEST_H_
#define G1_UNIT_GUI_TEST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gui/gui.h"

/* 4 bpp, left pixel in the high nibble; guard rows catch draws past row 199. */
#define GUI_TEST_GUARD_ROWS 48
#define GUI_TEST_GUARD_FILL 0xa5

extern uint8_t *gui_test_rows[GUI_FB_ROWS + GUI_TEST_GUARD_ROWS];

void gui_test_fb_reset(void);
uint8_t gui_test_px(int x, int y);
void gui_test_px_set(int x, int y, uint8_t v);
int gui_test_lit(int x0, int y0, int x1, int y1);
bool gui_test_guard_intact(void);
void gui_test_fb_save(uint8_t *out);
bool gui_test_fb_equal(const uint8_t *saved);

enum gui_golden_fmt {
	GUI_PBM, /* levels 0 / 15 only */
	GUI_PGM,
};

/* Pixels outside the window must be 0; a mismatch is written to gui_actual/. */
void gui_expect_golden(const char *name, enum gui_golden_fmt fmt, int x, int y, int w, int h);

/* hand-drawn stand-ins for the external flash font */
#define CJK_ZHONG 0x4e2d /* 中 */
#define CJK_RI    0x65e5 /* 日 */
#define CJK_TIAN  0x7530 /* 田 */
#define CJK_KOU   0x53e3 /* 口 */
#define CJK_ER    0x4e8c /* 二, deliberately not in the stand-in font */

void gui_test_seed_cjk_font(void);
const uint8_t *gui_test_cjk_glyph(uint16_t code);

#endif /* G1_UNIT_GUI_TEST_H_ */
