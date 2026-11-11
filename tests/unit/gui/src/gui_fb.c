/* Frame buffer, golden comparison and stand-in CJK font. */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_storage.h"
#include "gui_test.h"

static uint8_t fb_mem[(GUI_FB_ROWS + GUI_TEST_GUARD_ROWS) * GUI_FB_ROW_BYTES];
uint8_t *gui_test_rows[GUI_FB_ROWS + GUI_TEST_GUARD_ROWS];

void gui_test_fb_reset(void)
{
	memset(fb_mem, 0, GUI_FB_ROWS * GUI_FB_ROW_BYTES);
	memset(&fb_mem[GUI_FB_ROWS * GUI_FB_ROW_BYTES], GUI_TEST_GUARD_FILL,
	       GUI_TEST_GUARD_ROWS * GUI_FB_ROW_BYTES);
	for (int i = 0; i < GUI_FB_ROWS + GUI_TEST_GUARD_ROWS; i++) {
		gui_test_rows[i] = &fb_mem[i * GUI_FB_ROW_BYTES];
	}
	set_frame_buffer(gui_test_rows);
}

uint8_t gui_test_px(int x, int y)
{
	uint8_t b = gui_test_rows[y][x / 2];

	return (x & 1) ? (b & 0x0f) : (b >> 4);
}

void gui_test_px_set(int x, int y, uint8_t v)
{
	uint8_t *b = &gui_test_rows[y][x / 2];

	if (x & 1) {
		*b = (*b & 0xf0) | (v & 0x0f);
	} else {
		*b = (*b & 0x0f) | (uint8_t)(v << 4);
	}
}

int gui_test_lit(int x0, int y0, int x1, int y1)
{
	int n = 0;

	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			n += gui_test_px(x, y) != 0;
		}
	}
	return n;
}

bool gui_test_guard_intact(void)
{
	for (size_t i = GUI_FB_ROWS * GUI_FB_ROW_BYTES; i < sizeof(fb_mem); i++) {
		if (fb_mem[i] != GUI_TEST_GUARD_FILL) {
			return false;
		}
	}
	return true;
}

void gui_test_fb_save(uint8_t *out)
{
	memcpy(out, fb_mem, GUI_FB_ROWS * GUI_FB_ROW_BYTES);
}

bool gui_test_fb_equal(const uint8_t *saved)
{
	return memcmp(saved, fb_mem, GUI_FB_ROWS * GUI_FB_ROW_BYTES) == 0;
}

struct gui_golden_file {
	const char *name;
	const uint8_t *data;
	size_t len;
};
extern const struct gui_golden_file gui_golden_files[];
extern const size_t gui_golden_file_count;

int g1_gui_host_write(const char *path, const void *data, unsigned long len);

#define IMG_MAX (GUI_SCREEN_WIDTH * GUI_FB_ROWS + 64)

static uint8_t img_actual[IMG_MAX];
static char msg[512];

static const struct gui_golden_file *golden_find(const char *file)
{
	for (size_t i = 0; i < gui_golden_file_count; i++) {
		if (strcmp(gui_golden_files[i].name, file) == 0) {
			return &gui_golden_files[i];
		}
	}
	return NULL;
}

static int pnm_int(const uint8_t *d, size_t len, size_t *pos)
{
	int v = 0;
	bool digits = false;

	while (*pos < len) {
		uint8_t c = d[*pos];

		if (c == '#') {
			while (*pos < len && d[*pos] != '\n') {
				(*pos)++;
			}
		} else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			(*pos)++;
		} else {
			break;
		}
	}
	while (*pos < len && d[*pos] >= '0' && d[*pos] <= '9') {
		v = v * 10 + (d[*pos] - '0');
		(*pos)++;
		digits = true;
	}
	return digits ? v : -1;
}

static size_t render(enum gui_golden_fmt fmt, int x0, int y0, int w, int h, uint8_t *out)
{
	size_t n = (size_t)snprintf((char *)out, 64, fmt == GUI_PBM ? "P4\n%d %d\n" : "P5\n%d %d\n15\n",
				    w, h);

	for (int y = y0; y < y0 + h; y++) {
		if (fmt == GUI_PBM) {
			int stride = (w + 7) / 8;

			memset(&out[n], 0, stride);
			for (int x = 0; x < w; x++) {
				if (gui_test_px(x0 + x, y) != 0) {
					out[n + x / 8] |= 0x80 >> (x % 8);
				}
			}
			n += stride;
		} else {
			for (int x = 0; x < w; x++) {
				out[n++] = gui_test_px(x0 + x, y);
			}
		}
	}
	return n;
}

static void write_actual(const char *file, size_t len)
{
	char path[256];

	snprintf(path, sizeof(path), "%s/%s", G1_GUI_ACTUAL_DIR, file);
	if (g1_gui_host_write(path, img_actual, len) != 0) {
		printk("gui: could not write %s\n", path);
	} else {
		printk("gui: render written to %s\n", path);
	}
}

void gui_expect_golden(const char *name, enum gui_golden_fmt fmt, int x0, int y0, int w, int h)
{
	char file[96];
	const struct gui_golden_file *g;
	size_t len;
	size_t pos = 0;
	int diff = 0;
	int fx = -1, fy = -1, fg = 0, fa = 0;

	zassert_true(x0 >= 0 && y0 >= 0 && w > 0 && h > 0 && x0 + w <= GUI_SCREEN_WIDTH &&
			     y0 + h <= GUI_FB_ROWS,
		     "golden window outside the frame");
	snprintf(file, sizeof(file), "%s.%s", name, fmt == GUI_PBM ? "pbm" : "pgm");

	for (int y = 0; y < GUI_FB_ROWS; y++) {
		for (int x = 0; x < GUI_SCREEN_WIDTH; x++) {
			bool inside = x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;

			zassert_true(inside || gui_test_px(x, y) == 0,
				     "%s: pixel (%d, %d) = %d outside the window (%d, %d) %dx%d", file, x,
				     y, gui_test_px(x, y), x0, y0, w, h);
			zassert_true(!inside || fmt != GUI_PBM || gui_test_px(x, y) == 0 ||
					     gui_test_px(x, y) == 15,
				     "%s: pixel (%d, %d) has level %d, a PBM golden holds 0 / 15 only",
				     file, x, y, gui_test_px(x, y));
		}
	}
	zassert_true(gui_test_guard_intact(), "%s: written past row %d", file, GUI_FB_ROWS - 1);

	len = render(fmt, x0, y0, w, h, img_actual);
	g = golden_find(file);
	if (g == NULL) {
		write_actual(file, len);
		zassert_unreachable("no golden %s; render written to %s/%s", file, G1_GUI_ACTUAL_DIR,
				    file);
	}

	pos = 2;
	int gw = pnm_int(g->data, g->len, &pos);
	int gh = pnm_int(g->data, g->len, &pos);

	if (fmt == GUI_PGM) {
		(void)pnm_int(g->data, g->len, &pos);
	}
	pos++;
	if (gw != w || gh != h || g->data[0] != 'P' || g->data[1] != (fmt == GUI_PBM ? '4' : '5')) {
		write_actual(file, len);
		zassert_unreachable("%s: golden is %c%c %dx%d, render is %dx%d", file, g->data[0],
				    g->data[1], gw, gh, w, h);
	}

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int gv, av = gui_test_px(x0 + x, y0 + y);

			if (fmt == GUI_PBM) {
				gv = (g->data[pos + y * ((w + 7) / 8) + x / 8] & (0x80 >> (x % 8))) ? 15 : 0;
			} else {
				gv = g->data[pos + y * w + x];
			}
			if (gv != av) {
				if (diff == 0) {
					fx = x0 + x;
					fy = y0 + y;
					fg = gv;
					fa = av;
				}
				diff++;
			}
		}
	}
	if (diff != 0) {
		write_actual(file, len);
		snprintf(msg, sizeof(msg),
			 "%s: %d pixel(s) differ, first at frame (%d, %d): golden %d, render %d; "
			 "render written to %s/%s",
			 file, diff, fx, fy, fg, fa, G1_GUI_ACTUAL_DIR, file);
		zassert_unreachable("%s", msg);
	}
}

#define CJK_W 18
#define CJK_H 26
#define CJK_GLYPH_BYTES (CJK_W / 2 * CJK_H)

struct cjk_art {
	uint16_t code;
	const char *rows[CJK_H];
};

/* '#' = lit */
static const struct cjk_art cjk_art[] = {
	{CJK_ZHONG,
	 {"..................", "..................", "........##........", "........##........",
	  "........##........", "..##############..", "..##############..", "..##....##....##..",
	  "..##....##....##..", "..##....##....##..", "..##....##....##..", "..##....##....##..",
	  "..##....##....##..", "..##############..", "..##############..", "........##........",
	  "........##........", "........##........", "........##........", "........##........",
	  "........##........", "........##........", "........##........", "........##........",
	  "..................", ".................."}},
	{CJK_RI,
	 {"..................", "..................", "..................", "....##########....",
	  "....##########....", "....##......##....", "....##......##....", "....##......##....",
	  "....##......##....", "....##......##....", "....##......##....", "....##########....",
	  "....##########....", "....##......##....", "....##......##....", "....##......##....",
	  "....##......##....", "....##......##....", "....##......##....", "....##......##....",
	  "....##########....", "....##########....", "..................", "..................",
	  "..................", ".................."}},
	{CJK_TIAN,
	 {"..................", "..................", "..................", "..................",
	  "..##############..", "..##############..", "..##....##....##..", "..##....##....##..",
	  "..##....##....##..", "..##....##....##..", "..##....##....##..", "..##############..",
	  "..##############..", "..##....##....##..", "..##....##....##..", "..##....##....##..",
	  "..##....##....##..", "..##....##....##..", "..##....##....##..", "..##############..",
	  "..##############..", "..................", "..................", "..................",
	  "..................", ".................."}},
	{CJK_KOU,
	 {"..................", "..................", "..................", "..................",
	  "..................", "..................", "...############...", "...############...",
	  "...##........##...", "...##........##...", "...##........##...", "...##........##...",
	  "...##........##...", "...##........##...", "...##........##...", "...##........##...",
	  "...##........##...", "...##........##...", "...############...", "...############...",
	  "..................", "..................", "..................", "..................",
	  "..................", ".................."}},
};

static uint8_t cjk_glyphs[ARRAY_SIZE(cjk_art)][CJK_GLYPH_BYTES];

const uint8_t *gui_test_cjk_glyph(uint16_t code)
{
	for (size_t i = 0; i < ARRAY_SIZE(cjk_art); i++) {
		if (cjk_art[i].code == code) {
			return cjk_glyphs[i];
		}
	}
	return NULL;
}

void gui_test_seed_cjk_font(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(cjk_art); i++) {
		uint8_t *g = cjk_glyphs[i];
		uint32_t idx_addr = 0x140000 + (cjk_art[i].code - 0xa4) * 4;

		memset(g, 0, CJK_GLYPH_BYTES);
		for (int y = 0; y < CJK_H; y++) {
			for (int x = 0; x < CJK_W; x++) {
				if (cjk_art[i].rows[y][x] == '#') {
					g[y * (CJK_W / 2) + x / 2] |= (x & 1) ? 0x0f : 0xf0;
				}
			}
		}
		/* halfword 1 = glyph number */
		g1_fake_flash_image[idx_addr + 0] = 0;
		g1_fake_flash_image[idx_addr + 1] = 0;
		g1_fake_flash_image[idx_addr + 2] = (uint8_t)i;
		g1_fake_flash_image[idx_addr + 3] = 0;
		memcpy(&g1_fake_flash_image[0x200000 + i * CJK_GLYPH_BYTES], g, CJK_GLYPH_BYTES);
	}
}
