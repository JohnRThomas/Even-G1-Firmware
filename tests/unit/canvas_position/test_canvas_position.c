/* Canvas placement for the height and depth settings (0x26 sub 2), through the real panel flush. */
#define GUI_C_INTERNAL /* gui_screen_x_offset() returns int */

#include "fakes.h"

#include "dashboard/dashboard.h"
#include "panel_display/panel.h"
#include "ui/ui.h"

#define PANEL_ROWS 480 /* datasheet Table 1 */

#define CANVAS_W     0x240
#define CANVAS_H     0x88
#define CANVAS_LEVEL 0xf

#define HEIGHT_MIN  0
#define HEIGHT_MAX  8
#define HEIGHT_STEP ((PANEL_ROWS - PANEL_FB_ROWS) / (HEIGHT_MAX - HEIGHT_MIN))
#define DEPTH_MIN   1
#define DEPTH_MAX   9
#define DEPTH_FLAT  3

#define CANVAS_X_DEFAULT 32
#define CANVAS_Y_DEFAULT 32
#define CANVAS_X_MIN     22
#define CANVAS_X_MAX     42
#define CANVAS_Y_MIN     4
#define CANVAS_Y_MAX     64

/* slave's shift per depth gear; the master's is negated */
static const int depth_shift[DEPTH_MAX + 1] = {
  [1] = 22, [2] = 8, [3] = 0, [4] = -4, [5] = -8, [6] = -10, [7] = -12, [8] = -14, [9] = -16,
};

FAKE_VALUE_FUNC(int, ui_DashBoard_task, struct jbd_panel_context *, void *, int);
FAKE_VOID_FUNC(set_screen_cleared);

/* 5 command-header bytes before, 3 spare after */
static uint8_t fb_mem[5 + PANEL_FB_SIZE + 3];

static uint8_t panel_img[PANEL_ROWS][PANEL_WIDTH];
static int off_panel_lit;

struct box {
  int x0, y0, x1, y1;
  int lit;
};

static const char *role_name(int role) {
  return role == G1_ROLE_MASTER ? "master" : "slave";
}

static int expected_x(int role, int depth, int canvas_x) {
  return canvas_x + (role == G1_ROLE_MASTER ? -depth_shift[depth] : depth_shift[depth]);
}

static int expected_y(int height, int canvas_y) {
  return (HEIGHT_MAX - height) * HEIGHT_STEP + canvas_y;
}

static void panel_put(int col, int row, uint8_t level) {
  if (col >= PANEL_WIDTH || row >= PANEL_ROWS) {
    off_panel_lit += level != 0;
    return;
  }
  panel_img[row][col] = level;
}

/* cache write 02H: {ROW[8:0], COL[9:0]}, dummy, 4-bit pixels high nibble first */
static int panel_bus_xfer(struct spi_master_ctx *ctx, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len) {
  uint32_t addr;
  int col, row;

  ARG_UNUSED(ctx);
  ARG_UNUSED(rx);
  ARG_UNUSED(rx_len);

  if (tx_len < 5 || tx[0] != SPI_WR_BUFFER) {
    return 0;
  }
  addr = sys_get_be24(&tx[1]);
  col = addr & 0x3ff;
  row = (addr >> 10) & 0x1ff;
  for (size_t index = 5; index < tx_len; index++) {
    panel_put(col, row, tx[index] >> 4);
    panel_put(col + 1, row, tx[index] & 0x0f);
    col += 2;
    if (col >= PANEL_WIDTH) {
      col = 0;
      row++;
    }
  }
  return 0;
}

static struct box lit_box(void) {
  struct box area = {PANEL_WIDTH, PANEL_ROWS, -1, -1, 0};

  for (int row = 0; row < PANEL_ROWS; row++) {
    for (int col = 0; col < PANEL_WIDTH; col++) {
      if (panel_img[row][col] == 0) {
        continue;
      }
      area.lit++;
      area.x0 = MIN(area.x0, col);
      area.y0 = MIN(area.y0, row);
      area.x1 = MAX(area.x1, col);
      area.y1 = MAX(area.y1, row);
    }
  }
  return area;
}

/* the lit area is the whole canvas at panel (left, top) and nothing is addressed off the panel */
#define expect_canvas_at(area, left, top, fmt, ...)                                                                    \
  do {                                                                                                                 \
    zassert_equal(off_panel_lit, 0, fmt ": %d lit pixels addressed beyond the panel", ##__VA_ARGS__, off_panel_lit);   \
    zassert_equal((area).lit, CANVAS_W * CANVAS_H, fmt ": %d pixels lit, the canvas has %d", ##__VA_ARGS__,            \
                  (area).lit, CANVAS_W * CANVAS_H);                                                                    \
    zassert_true((area).x0 == (left) && (area).y0 == (top) && (area).x1 == (left) + CANVAS_W - 1 &&                    \
                   (area).y1 == (top) + CANVAS_H - 1,                                                                  \
                 fmt ": canvas at (%d, %d)..(%d, %d), expected (%d, %d)..(%d, %d)", ##__VA_ARGS__, (area).x0,          \
                 (area).y0, (area).x1, (area).y1, (left), (top), (left) + CANVAS_W - 1, (top) + CANVAS_H - 1);         \
  } while (0)

static void draw_canvas(struct jbd_panel_context *panel) {
  int x0 = gui_screen_x_offset();
  int y0 = gui_screen_y_offset();
  uint8_t lit = (CANVAS_LEVEL << 4) | CANVAS_LEVEL;

  zassert_true(_clean_fb_data(panel->panel_buffer, lit, x0, y0, x0 + CANVAS_W - 1, y0 + CANVAS_H - 1) > 0,
               "canvas (%d, %d) does not fit the frame", x0, y0);
  zassert_equal(
    _reflash_fb_data_to_lcd(panel->current_row, panel->current_column, 0, 0, PANEL_WIDTH, PANEL_FB_ROWS - 1), 0);
}

static int dashboard_draws_canvas(struct jbd_panel_context *panel, void *arg, int event) {
  ARG_UNUSED(arg);

  if (event == UI_EVT_ENTER) {
    draw_canvas(panel);
  }
  return 0;
}

static struct box show(int role, int height, int depth) {
  struct GlassesState *gs = g1_state();

  g1_unit_set_role(role);
  gs->panel.raster_height_offset = height;
  gs->panel.canvas_distance_gear = depth;
  cal_panel_canvas_coord((int *)&gs->panel.y_offset, (int *)&gs->panel.current_column);
  zassert_equal(gui_screen_clear(), 0);
  draw_canvas(&gs->panel);
  return lit_box();
}

/* 0x26 sub 2 with preview 1 */
static struct box preview(int height, int depth, int event) {
  struct GlassesState *gs = g1_state();

  gs->panel.raster_height_offset = height;
  gs->panel.canvas_distance_gear = depth;
  zassert_equal(ui_raster_height_task(&gs->panel, NULL, event), 0);
  return lit_box();
}

static void canvas_position_before(void *fixture) {
  struct GlassesState *gs = g1_state();
  struct jbd_panel_context *panel = &gs->panel;

  ARG_UNUSED(fixture);

  memset(fb_mem, 0, sizeof(fb_mem));
  panel->current_buffer = fb_mem + 5;
  for (int row = 0; row < PANEL_FB_ROWS; row++) {
    panel->panel_buffer[row] = fb_mem + 5 + row * PANEL_FB_STRIDE;
  }
  panel->current_buffer_size = PANEL_FB_SIZE;
  k_mutex_init(&lcd_mutex);
  gs->spi.xfer_cb = panel_bus_xfer;
  panel->spi_callbacks = &gs->spi;
  panel->init_done = 1;

  gs->canvas_x = CANVAS_X_DEFAULT;
  gs->canvas_y = CANVAS_Y_DEFAULT;
  panel->raster_height_offset = 3;
  panel->canvas_distance_gear = DEPTH_FLAT;

  /* the preview page forgets its position on exit */
  RESET_FAKE(ui_DashBoard_task);
  RESET_FAKE(set_screen_cleared);
  (void)ui_raster_height_task(panel, NULL, UI_EVT_EXIT);
  RESET_FAKE(ui_DashBoard_task);
  ui_DashBoard_task_fake.custom_fake = dashboard_draws_canvas;

  memset(panel_img, 0, sizeof(panel_img));
  off_panel_lit = 0;

  /* each gear change logs a warning */
  LOG_LEVEL = 1;
}

ZTEST_SUITE(canvas_position, NULL, NULL, canvas_position_before, NULL, NULL);

ZTEST(canvas_position, test_lowest_height_puts_the_last_frame_row_on_the_last_panel_row) {
  struct box area;

  /* 64 + 135 = 199, the last frame row */
  g1_state()->canvas_y = CANVAS_Y_MAX;

  area = show(G1_ROLE_MASTER, HEIGHT_MIN, DEPTH_FLAT);

  expect_canvas_at(area, CANVAS_X_DEFAULT, PANEL_ROWS - PANEL_FB_ROWS + CANVAS_Y_MAX, "height 0");
  zassert_equal(area.y1, PANEL_ROWS - 1);
}

ZTEST(canvas_position, test_each_height_gear_moves_the_canvas_35_rows_on_both_lenses) {
  for (int role = G1_ROLE_MASTER; role <= G1_ROLE_SLAVE; role++) {
    for (int height = HEIGHT_MIN; height <= HEIGHT_MAX; height++) {
      struct box area = show(role, height, DEPTH_FLAT);

      expect_canvas_at(area, CANVAS_X_DEFAULT, (HEIGHT_MAX - height) * 35 + CANVAS_Y_DEFAULT, "%s height %d",
                       role_name(role), height);
    }
  }
}

ZTEST(canvas_position, test_default_depth_leaves_each_canvas_at_its_calibrated_x) {
  struct box area;

  g1_state()->canvas_x = 26;
  area = show(G1_ROLE_MASTER, HEIGHT_MAX, DEPTH_FLAT);
  expect_canvas_at(area, 26, CANVAS_Y_DEFAULT, "master depth 3");

  g1_state()->canvas_x = 38;
  area = show(G1_ROLE_SLAVE, HEIGHT_MAX, DEPTH_FLAT);
  expect_canvas_at(area, 38, CANVAS_Y_DEFAULT, "slave depth 3");
}

ZTEST(canvas_position, test_depth_gears_shift_the_canvas_by_the_depth_table) {
  for (int role = G1_ROLE_MASTER; role <= G1_ROLE_SLAVE; role++) {
    for (int depth = DEPTH_MIN; depth <= DEPTH_MAX; depth++) {
      struct box area = show(role, 4, depth);

      expect_canvas_at(area, expected_x(role, depth, CANVAS_X_DEFAULT), expected_y(4, CANVAS_Y_DEFAULT), "%s depth %d",
                       role_name(role), depth);
    }
  }
}

ZTEST(canvas_position, test_canvas_stays_whole_on_the_panel_at_every_height_and_depth) {
  /* the calibration extremes */
  static const struct {
    int canvas_x;
    int canvas_y;
  } corners[] = {
    {CANVAS_X_MIN, CANVAS_Y_MIN},
    {CANVAS_X_MIN, CANVAS_Y_MAX},
    {CANVAS_X_MAX, CANVAS_Y_MIN},
    {CANVAS_X_MAX, CANVAS_Y_MAX},
  };
  struct GlassesState *gs = g1_state();
  struct box reach = {PANEL_WIDTH, PANEL_ROWS, -1, -1, 0};

  for (int role = G1_ROLE_MASTER; role <= G1_ROLE_SLAVE; role++) {
    for (size_t corner = 0; corner < ARRAY_SIZE(corners); corner++) {
      int canvas_x = corners[corner].canvas_x;
      int canvas_y = corners[corner].canvas_y;

      for (int height = HEIGHT_MIN; height <= HEIGHT_MAX; height++) {
        for (int depth = DEPTH_MIN; depth <= DEPTH_MAX; depth++) {
          struct box area;

          gs->canvas_x = canvas_x;
          gs->canvas_y = canvas_y;
          area = show(role, height, depth);
          expect_canvas_at(area, expected_x(role, depth, canvas_x), expected_y(height, canvas_y),
                           "%s canvas (%d, %d) height %d depth %d", role_name(role), canvas_x, canvas_y, height, depth);
          reach.x0 = MIN(reach.x0, area.x0);
          reach.y0 = MIN(reach.y0, area.y0);
          reach.x1 = MAX(reach.x1, area.x1);
          reach.y1 = MAX(reach.y1, area.y1);
        }
      }
    }
  }

  /* the top edge less the 4-row canvas_y minimum */
  zassert_equal(reach.x0, 0);
  zassert_equal(reach.x1, PANEL_WIDTH - 1);
  zassert_equal(reach.y0, CANVAS_Y_MIN);
  zassert_equal(reach.y1, PANEL_ROWS - 1);
}

ZTEST(canvas_position, test_height_preview_moves_the_canvas_from_top_to_bottom) {
  struct box area = preview(HEIGHT_MAX, DEPTH_FLAT, UI_EVT_ENTER);

  expect_canvas_at(area, CANVAS_X_DEFAULT, expected_y(HEIGHT_MAX, CANVAS_Y_DEFAULT), "preview height 8");

  /* the old canvas is cleared */
  area = preview(HEIGHT_MIN, DEPTH_FLAT, UI_EVT_REFRESH);
  expect_canvas_at(area, CANVAS_X_DEFAULT, expected_y(HEIGHT_MIN, CANVAS_Y_DEFAULT), "preview height 0");
  zassert_equal(ui_DashBoard_task_fake.call_count, 2);
}

ZTEST(canvas_position, test_depth_preview_moves_the_canvases_in_and_out_on_both_lenses) {
  for (int role = G1_ROLE_MASTER; role <= G1_ROLE_SLAVE; role++) {
    struct box area;

    g1_unit_set_role(role);

    area = preview(4, DEPTH_MIN, UI_EVT_ENTER);
    expect_canvas_at(area, expected_x(role, DEPTH_MIN, CANVAS_X_DEFAULT), expected_y(4, CANVAS_Y_DEFAULT),
                     "%s preview depth 1", role_name(role));

    area = preview(4, DEPTH_MAX, UI_EVT_REFRESH);
    expect_canvas_at(area, expected_x(role, DEPTH_MAX, CANVAS_X_DEFAULT), expected_y(4, CANVAS_Y_DEFAULT),
                     "%s preview depth 9", role_name(role));

    (void)ui_raster_height_task(&g1_state()->panel, NULL, UI_EVT_EXIT);
  }
}
