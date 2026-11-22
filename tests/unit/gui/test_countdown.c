/*
 * Template countdown page (screen id 2): the hourglass, then the remaining time as MM:SS, or
 * HH:MM:SS when an hour or more is left. Each character is a 12 x 36 ASCII cell at row 12; x is
 * a byte column (two pixels).
 */
#include "fonts/font_bitmap.h"
#include "gui_test.h"

#include "notify/ui_countdown.c"

#define CELL_W   12
#define CELL_H   36
#define CELL_ROW 12

static int running_status_calls;

/* ui_quicknote.c is not in this suite */
void draw_countdown_running_status(uint8_t **fb, void *arg, int a, int b, int c) {
  ARG_UNUSED(fb);
  ARG_UNUSED(arg);
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  running_status_calls++;
}

static void draw_countdown(uint32_t seconds) {
  struct GlassesState *state = get_dashboard_state();

  for (size_t row = 0; row < MIN(ARRAY_SIZE(state->panel.panel_buffer), GUI_FB_ROWS); row++) {
    state->panel.panel_buffer[row] = gui_test_rows[row];
  }
  state->ui.timer.seconds = seconds;
  state->ui.timer.state = 1;
  running_status_calls = 0;
  zassert_equal(draw_template_countdown_page(&state->panel, NULL), 0);
  zassert_equal(running_status_calls, 1);
  zassert_true(gui_test_guard_intact());
}

/* the cells at the byte columns x[] hold the glyphs of text */
static void expect_cells(const char *text, const int *x) {
  uint8_t want[CELL_W * CELL_H];

  for (size_t index = 0; text[index] != '\0'; index++) {
    memcpy(want, find_chinese_bitmap_by_unicode(text[index], CELL_W, CELL_H), sizeof(want));
    for (int row = 0; row < CELL_H; row++) {
      zassert_mem_equal(gui_test_rows[CELL_ROW + row] + x[index], want + row * CELL_W, CELL_W,
            "'%c' (cell %zu) row %d", text[index], index, row);
    }
    zassert_true(gui_test_lit(x[index] * 2, CELL_ROW, (x[index] + CELL_W) * 2, CELL_ROW + CELL_H) > 0,
           "'%c' (cell %zu) is blank", text[index], index);
  }
}

ZTEST(gui, test_countdown_page_draws_minutes_and_seconds) {
  static const int x[] = {32, 47, 62, 77, 92};

  draw_countdown(125);
  expect_cells("02:05", x);
}

ZTEST(gui, test_countdown_page_draws_hours_when_not_zero) {
  static const int x[] = {32, 47, 62, 77, 92, 107, 122, 137};

  draw_countdown(3600 + 23 * 60 + 45);
  expect_cells("01:23:45", x);
}

/* each second gives another picture */
ZTEST(gui, test_countdown_page_changes_with_the_remaining_time) {
  draw_countdown(59);
  gui_test_fb_save_and_reset();
  draw_countdown(58);
  zassert_false(gui_test_fb_matches_saved());
  gui_test_fb_save_and_reset();
  draw_countdown(58);
  zassert_true(gui_test_fb_matches_saved());
}
