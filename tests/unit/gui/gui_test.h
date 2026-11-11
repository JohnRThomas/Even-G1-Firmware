/* gui suite: setup, frame buffer, golden images and the stand-in CJK font (gui_fb.c). */
#ifndef G1_UNIT_GUI_TEST_H_
#define G1_UNIT_GUI_TEST_H_

#include "fakes.h"
#include "gui/gui.h"

/* 4 bpp, left pixel in the high nibble; guard rows catch draws past row 199. */
#define GUI_TEST_GUARD_ROWS 48
#define GUI_TEST_GUARD_FILL 0xa5

extern uint8_t *gui_test_rows[GUI_FB_ROWS + GUI_TEST_GUARD_ROWS];

void gui_test_fb_reset(void);
uint8_t gui_test_px(int col, int row);
void gui_test_px_set(int col, int row, uint8_t level);
/* lit pixels in columns x0..x1 - 1 of rows y0..y1 - 1 */
int gui_test_lit(int x0, int y0, int x1, int y1);
bool gui_test_guard_intact(void);
/* keeps a copy of the frame for gui_test_fb_matches_saved(), then clears it */
void gui_test_fb_save_and_reset(void);
bool gui_test_fb_matches_saved(void);

enum gui_golden_fmt {
  GUI_PBM, /* levels 0 / 15 only */
  GUI_PGM,
};

/* Pixels outside the window must be 0; a mismatch is written to gui_actual/. */
void gui_expect_golden(const char *name, enum gui_golden_fmt fmt, int x0, int y0, int width,
           int height);

/* hand-drawn stand-ins for the external flash font */
#define CJK_ZHONG 0x4e2d /* 中 */
#define CJK_RI    0x65e5 /* 日 */
#define CJK_TIAN  0x7530 /* 田 */
#define CJK_KOU   0x53e3 /* 口 */
#define CJK_ER    0x4e8c /* 二, deliberately not in the stand-in font */

const uint8_t *gui_test_cjk_glyph(uint16_t code);

#endif /* G1_UNIT_GUI_TEST_H_ */
