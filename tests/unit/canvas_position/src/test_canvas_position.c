#define GUI_C_INTERNAL /* gui_screen_x_offset() returns int */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "harness.h"

#include "dashboard/dashboard.h"
#include "gui/gui.h"
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

static const int roles[] = {G1_ROLE_MASTER, G1_ROLE_SLAVE};

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

static const char *role_name(int role)
{
	return role == G1_ROLE_MASTER ? "master" : "slave";
}

static int expected_x(int role, int depth, int canvas_x)
{
	return canvas_x + (role == G1_ROLE_MASTER ? -depth_shift[depth] : depth_shift[depth]);
}

static int expected_y(int height, int canvas_y)
{
	return (HEIGHT_MAX - height) * HEIGHT_STEP + canvas_y;
}

static void panel_put(int x, int y, uint8_t level)
{
	if (x >= PANEL_WIDTH || y >= PANEL_ROWS) {
		off_panel_lit += level != 0;
		return;
	}
	panel_img[y][x] = level;
}

/* cache write 02H: {ROW[8:0], COL[9:0]}, dummy, 4-bit pixels high nibble first */
static int panel_bus_xfer(struct spi_master_ctx *ctx, const uint8_t *tx, size_t tx_len,
			  uint8_t *rx, size_t rx_len)
{
	uint32_t addr;
	int x, y;

	ARG_UNUSED(ctx);
	ARG_UNUSED(rx);
	ARG_UNUSED(rx_len);

	if (tx_len < 5 || tx[0] != SPI_WR_BUFFER) {
		return 0;
	}
	addr = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
	x = addr & 0x3ff;
	y = (addr >> 10) & 0x1ff;
	for (size_t i = 5; i < tx_len; i++) {
		panel_put(x, y, tx[i] >> 4);
		panel_put(x + 1, y, tx[i] & 0x0f);
		x += 2;
		if (x >= PANEL_WIDTH) {
			x = 0;
			y++;
		}
	}
	return 0;
}

static struct box lit_box(void)
{
	struct box b = {PANEL_WIDTH, PANEL_ROWS, -1, -1, 0};

	for (int y = 0; y < PANEL_ROWS; y++) {
		for (int x = 0; x < PANEL_WIDTH; x++) {
			if (panel_img[y][x] == 0) {
				continue;
			}
			b.lit++;
			b.x0 = MIN(b.x0, x);
			b.y0 = MIN(b.y0, y);
			b.x1 = MAX(b.x1, x);
			b.y1 = MAX(b.y1, y);
		}
	}
	return b;
}

#define expect_canvas_at(b, x, y, fmt, ...)                                                        \
	do {                                                                                       \
		zassert_equal(off_panel_lit, 0, fmt ": %d lit pixels addressed beyond the panel",  \
			      ##__VA_ARGS__, off_panel_lit);                                       \
		zassert_equal((b).lit, CANVAS_W * CANVAS_H,                                        \
			      fmt ": %d pixels lit, the canvas has %d", ##__VA_ARGS__, (b).lit,    \
			      CANVAS_W * CANVAS_H);                                                \
		zassert_true((b).x0 == (x) && (b).y0 == (y) && (b).x1 == (x) + CANVAS_W - 1 &&     \
				     (b).y1 == (y) + CANVAS_H - 1,                                 \
			     fmt ": canvas on panel (%d, %d)..(%d, %d), expected (%d, %d)..(%d, %d)", \
			     ##__VA_ARGS__, (b).x0, (b).y0, (b).x1, (b).y1, (x), (y),              \
			     (x) + CANVAS_W - 1, (y) + CANVAS_H - 1);                              \
	} while (0)

static void draw_canvas(struct jbd_panel_context *p)
{
	int x0 = gui_screen_x_offset();
	int y0 = gui_screen_y_offset();
	uint8_t lit = (CANVAS_LEVEL << 4) | CANVAS_LEVEL;

	zassert_true(_clean_fb_data(p->panel_buffer, lit, x0, y0, x0 + CANVAS_W - 1,
				    y0 + CANVAS_H - 1) > 0,
		     "canvas (%d, %d) does not fit the frame", x0, y0);
	zassert_equal(_reflash_fb_data_to_lcd(p->current_row, p->current_column, 0, 0, PANEL_WIDTH,
					      PANEL_FB_ROWS - 1),
		      0);
}

static int dashboard_draws_canvas(struct jbd_panel_context *panel, void *arg, int event)
{
	ARG_UNUSED(arg);

	if (event == UI_EVT_ENTER) {
		draw_canvas(panel);
	}
	return 0;
}

static struct box show(int role, int height, int depth)
{
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
static struct box preview(int height, int depth, int event)
{
	struct GlassesState *gs = g1_state();

	gs->panel.raster_height_offset = height;
	gs->panel.canvas_distance_gear = depth;
	zassert_equal(ui_raster_height_task(&gs->panel, NULL, event), 0);
	return lit_box();
}

static void canvas_position_before(void *fixture)
{
	struct GlassesState *gs = g1_state();
	struct jbd_panel_context *p = &gs->panel;

	ARG_UNUSED(fixture);

	memset(fb_mem, 0, sizeof(fb_mem));
	p->current_buffer = fb_mem + 5;
	for (int i = 0; i < PANEL_FB_ROWS; i++) {
		p->panel_buffer[i] = fb_mem + 5 + i * PANEL_FB_STRIDE;
	}
	p->current_buffer_size = PANEL_FB_SIZE;
	k_mutex_init(&lcd_mutex);
	gs->spi.xfer_cb = panel_bus_xfer;
	p->spi_callbacks = &gs->spi;
	p->init_done = 1;
	p->current_row = 0;

	gs->canvas_x = CANVAS_X_DEFAULT;
	gs->canvas_y = CANVAS_Y_DEFAULT;
	p->raster_height_offset = 3;
	p->canvas_distance_gear = DEPTH_FLAT;

	/* the preview page forgets its position on exit */
	RESET_FAKE(ui_DashBoard_task);
	RESET_FAKE(set_screen_cleared);
	(void)ui_raster_height_task(p, NULL, UI_EVT_EXIT);
	RESET_FAKE(ui_DashBoard_task);
	ui_DashBoard_task_fake.custom_fake = dashboard_draws_canvas;

	memset(panel_img, 0, sizeof(panel_img));
	off_panel_lit = 0;

	/* each gear change logs a warning */
	LOG_LEVEL = 1;
}

ZTEST_SUITE(canvas_position, NULL, NULL, canvas_position_before, NULL, NULL);

/* ---- height ---- */

ZTEST(canvas_position, test_lowest_height_puts_the_last_frame_row_on_the_last_panel_row)
{
	struct box b;

	/* 64 + 135 = 199, the last frame row */
	g1_state()->canvas_y = CANVAS_Y_MAX;

	b = show(G1_ROLE_MASTER, HEIGHT_MIN, DEPTH_FLAT);

	expect_canvas_at(b, CANVAS_X_DEFAULT, PANEL_ROWS - PANEL_FB_ROWS + CANVAS_Y_MAX, "height 0");
	zassert_equal(b.y1, PANEL_ROWS - 1);
}

ZTEST(canvas_position, test_each_height_gear_moves_the_canvas_35_rows_on_both_lenses)
{
	for (size_t r = 0; r < ARRAY_SIZE(roles); r++) {
		for (int h = HEIGHT_MIN; h <= HEIGHT_MAX; h++) {
			struct box b = show(roles[r], h, DEPTH_FLAT);

			expect_canvas_at(b, CANVAS_X_DEFAULT, (HEIGHT_MAX - h) * 35 + CANVAS_Y_DEFAULT,
					 "%s height %d", role_name(roles[r]), h);
		}
	}
}

/* ---- depth ---- */

ZTEST(canvas_position, test_default_depth_leaves_each_canvas_at_its_calibrated_x)
{
	struct box b;

	g1_state()->canvas_x = 26;
	b = show(G1_ROLE_MASTER, HEIGHT_MAX, DEPTH_FLAT);
	expect_canvas_at(b, 26, CANVAS_Y_DEFAULT, "master depth 3");

	g1_state()->canvas_x = 38;
	b = show(G1_ROLE_SLAVE, HEIGHT_MAX, DEPTH_FLAT);
	expect_canvas_at(b, 38, CANVAS_Y_DEFAULT, "slave depth 3");
}

ZTEST(canvas_position, test_depth_gears_shift_the_canvas_by_the_depth_table)
{
	for (size_t r = 0; r < ARRAY_SIZE(roles); r++) {
		for (int d = DEPTH_MIN; d <= DEPTH_MAX; d++) {
			struct box b = show(roles[r], 4, d);

			expect_canvas_at(b, expected_x(roles[r], d, CANVAS_X_DEFAULT),
					 expected_y(4, CANVAS_Y_DEFAULT), "%s depth %d",
					 role_name(roles[r]), d);
		}
	}
}

ZTEST(canvas_position, test_canvas_stays_whole_on_the_panel_at_every_height_and_depth)
{
	static const int cal_x[] = {CANVAS_X_MIN, CANVAS_X_MAX};
	static const int cal_y[] = {CANVAS_Y_MIN, CANVAS_Y_MAX};
	struct GlassesState *gs = g1_state();
	struct box reach = {PANEL_WIDTH, PANEL_ROWS, -1, -1, 0};

	for (size_t r = 0; r < ARRAY_SIZE(roles); r++) {
		for (size_t i = 0; i < ARRAY_SIZE(cal_x); i++) {
			for (size_t j = 0; j < ARRAY_SIZE(cal_y); j++) {
				for (int h = HEIGHT_MIN; h <= HEIGHT_MAX; h++) {
					for (int d = DEPTH_MIN; d <= DEPTH_MAX; d++) {
						struct box b;

						gs->canvas_x = cal_x[i];
						gs->canvas_y = cal_y[j];
						b = show(roles[r], h, d);
						expect_canvas_at(b, expected_x(roles[r], d, cal_x[i]),
								 expected_y(h, cal_y[j]),
								 "%s canvas (%d, %d) height %d depth %d",
								 role_name(roles[r]), cal_x[i],
								 cal_y[j], h, d);
						reach.x0 = MIN(reach.x0, b.x0);
						reach.y0 = MIN(reach.y0, b.y0);
						reach.x1 = MAX(reach.x1, b.x1);
						reach.y1 = MAX(reach.y1, b.y1);
					}
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

/* ---- preview page ---- */

ZTEST(canvas_position, test_height_preview_moves_the_canvas_from_top_to_bottom)
{
	struct box b;

	b = preview(HEIGHT_MAX, DEPTH_FLAT, UI_EVT_ENTER);
	expect_canvas_at(b, CANVAS_X_DEFAULT, expected_y(HEIGHT_MAX, CANVAS_Y_DEFAULT),
			 "preview height 8");

	/* the old canvas is cleared */
	b = preview(HEIGHT_MIN, DEPTH_FLAT, UI_EVT_REFRESH);
	expect_canvas_at(b, CANVAS_X_DEFAULT, expected_y(HEIGHT_MIN, CANVAS_Y_DEFAULT),
			 "preview height 0");
	zassert_equal(ui_DashBoard_task_fake.call_count, 2);
}

ZTEST(canvas_position, test_depth_preview_moves_the_canvases_in_and_out_on_both_lenses)
{
	struct GlassesState *gs = g1_state();

	for (size_t r = 0; r < ARRAY_SIZE(roles); r++) {
		struct box b;

		g1_unit_set_role(roles[r]);

		b = preview(4, DEPTH_MIN, UI_EVT_ENTER);
		expect_canvas_at(b, expected_x(roles[r], DEPTH_MIN, CANVAS_X_DEFAULT),
				 expected_y(4, CANVAS_Y_DEFAULT), "%s preview depth 1",
				 role_name(roles[r]));

		b = preview(4, DEPTH_MAX, UI_EVT_REFRESH);
		expect_canvas_at(b, expected_x(roles[r], DEPTH_MAX, CANVAS_X_DEFAULT),
				 expected_y(4, CANVAS_Y_DEFAULT), "%s preview depth 9",
				 role_name(roles[r]));

		(void)ui_raster_height_task(&gs->panel, NULL, UI_EVT_EXIT);
	}
}
