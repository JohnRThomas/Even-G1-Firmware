/* The gui suite's setup, frame buffer, golden comparison and stand-in CJK font. */
#include "gui_test.h"

#define FRAME_BYTES (GUI_FB_ROWS * GUI_FB_ROW_BYTES)

static uint8_t fb_mem[(GUI_FB_ROWS + GUI_TEST_GUARD_ROWS) * GUI_FB_ROW_BYTES];
static uint8_t fb_saved[FRAME_BYTES];
uint8_t *gui_test_rows[GUI_FB_ROWS + GUI_TEST_GUARD_ROWS];

void gui_test_fb_reset(void)
{
  memset(fb_mem, 0, FRAME_BYTES);
  memset(&fb_mem[FRAME_BYTES], GUI_TEST_GUARD_FILL, GUI_TEST_GUARD_ROWS * GUI_FB_ROW_BYTES);
  for (int row = 0; row < GUI_FB_ROWS + GUI_TEST_GUARD_ROWS; row++) {
    gui_test_rows[row] = &fb_mem[row * GUI_FB_ROW_BYTES];
  }
  set_frame_buffer(gui_test_rows);
}

uint8_t gui_test_px(int col, int row)
{
  uint8_t pair = gui_test_rows[row][col / 2];

  return (col & 1) ? (pair & 0x0f) : (pair >> 4);
}

void gui_test_px_set(int col, int row, uint8_t level)
{
  uint8_t *pair = &gui_test_rows[row][col / 2];

  if (col & 1) {
    *pair = (*pair & 0xf0) | (level & 0x0f);
  } else {
    *pair = (*pair & 0x0f) | (uint8_t)(level << 4);
  }
}

int gui_test_lit(int x0, int y0, int x1, int y1)
{
  int count = 0;

  for (int row = y0; row < y1; row++) {
    for (int col = x0; col < x1; col++) {
      count += gui_test_px(col, row) != 0;
    }
  }
  return count;
}

bool gui_test_guard_intact(void)
{
  for (size_t index = FRAME_BYTES; index < sizeof(fb_mem); index++) {
    if (fb_mem[index] != GUI_TEST_GUARD_FILL) {
      return false;
    }
  }
  return true;
}

void gui_test_fb_save_and_reset(void)
{
  memcpy(fb_saved, fb_mem, FRAME_BYTES);
  gui_test_fb_reset();
}

bool gui_test_fb_matches_saved(void)
{
  return memcmp(fb_saved, fb_mem, FRAME_BYTES) == 0;
}

extern const struct g1_unit_file gui_goldens[];

/* common/host_io.c, in the native_sim runner */
int g1_host_write_file(const char *path, const void *data, unsigned long len);

static uint8_t rendered[GUI_SCREEN_WIDTH * GUI_FB_ROWS + 64];

/* the window as a P4 PBM (bit set = lit) or a P5 PGM of the levels */
static size_t render(enum gui_golden_fmt fmt, int x0, int y0, int width, int height)
{
  size_t len = (size_t)snprintf((char *)rendered, 64,
              fmt == GUI_PBM ? "P4\n%d %d\n" : "P5\n%d %d\n15\n", width,
              height);

  for (int row = y0; row < y0 + height; row++) {
    if (fmt == GUI_PBM) {
      int stride = (width + 7) / 8;

      memset(&rendered[len], 0, stride);
      for (int col = 0; col < width; col++) {
        if (gui_test_px(x0 + col, row) != 0) {
          rendered[len + col / 8] |= 0x80 >> (col % 8);
        }
      }
      len += stride;
    } else {
      for (int col = 0; col < width; col++) {
        rendered[len++] = gui_test_px(x0 + col, row);
      }
    }
  }
  return len;
}

/* goldens are renders accepted with update_goldens.py, so they are compared byte for byte */
void gui_expect_golden(const char *name, enum gui_golden_fmt fmt, int x0, int y0, int width,
           int height)
{
  static char diff[G1_MSG_SIZE];
  char file[96];
  char path[256];
  const struct g1_unit_file *golden = gui_goldens;
  size_t len;

  zassert_true(x0 >= 0 && y0 >= 0 && width > 0 && height > 0 &&
           x0 + width <= GUI_SCREEN_WIDTH && y0 + height <= GUI_FB_ROWS,
         "golden window outside the frame");
  snprintf(file, sizeof(file), "%s.%s", name, fmt == GUI_PBM ? "pbm" : "pgm");

  for (int row = 0; row < GUI_FB_ROWS; row++) {
    for (int col = 0; col < GUI_SCREEN_WIDTH; col++) {
      uint8_t level = gui_test_px(col, row);
      bool inside = col >= x0 && col < x0 + width && row >= y0 &&
              row < y0 + height;

      zassert_true(inside || level == 0,
             "%s: pixel (%d, %d) = %d outside the window (%d, %d) %dx%d",
             file, col, row, level, x0, y0, width, height);
      zassert_true(!inside || fmt != GUI_PBM || level == 0 || level == 15,
             "%s: pixel (%d, %d) has level %d; a PBM holds 0 / 15 only",
             file, col, row, level);
    }
  }
  zassert_true(gui_test_guard_intact(), "%s: written past row %d", file, GUI_FB_ROWS - 1);

  len = render(fmt, x0, y0, width, height);
  while (golden->name != NULL && strcmp(golden->name, file) != 0) {
    golden++;
  }
  if (golden->name == NULL ||
      !g1_bytes_check(rendered, len, golden->data, golden->len, diff, sizeof(diff))) {
    snprintf(path, sizeof(path), "%s/%s", G1_GUI_ACTUAL_DIR, file);
    if (g1_host_write_file(path, rendered, len) != 0) {
      printk("gui: could not write %s\n", path);
    }
    zassert_unreachable("%s: %s; render written to %s", file,
            golden->name == NULL ? "no golden" : diff, path);
  }
}

/* resource_manager.c's 18 x 26 font in the external flash: 4-byte index entries from U+00A4 */
#define CJK_INDEX_ADDR  0x140000
#define CJK_GLYPH_ADDR  0x200000
#define CJK_WIDTH       18
#define CJK_HEIGHT      26
#define CJK_GLYPH_BYTES (CJK_WIDTH / 2 * CJK_HEIGHT)

struct cjk_stroke {
  uint8_t col, row, width, height;
};

/* 2-pixel strokes, horizontal ones top to bottom, then vertical ones; glyph number = index */
static const struct {
  uint16_t code;
  struct cjk_stroke strokes[6]; /* unused entries are empty */
} cjk_font[] = {
  {CJK_ZHONG, {{2, 5, 14, 2}, {2, 13, 14, 2}, {2, 5, 2, 10}, {8, 2, 2, 22}, {14, 5, 2, 10}}},
  {CJK_RI, {{4, 3, 10, 2}, {4, 11, 10, 2}, {4, 20, 10, 2}, {4, 3, 2, 19}, {12, 3, 2, 19}}},
  {CJK_TIAN,
   {{2, 4, 14, 2}, {2, 11, 14, 2}, {2, 19, 14, 2}, {2, 4, 2, 17}, {8, 4, 2, 17},
    {14, 4, 2, 17}}},
  {CJK_KOU, {{3, 6, 12, 2}, {3, 18, 12, 2}, {3, 6, 2, 14}, {13, 6, 2, 14}}},
};

const uint8_t *gui_test_cjk_glyph(uint16_t code)
{
  for (size_t glyph = 0; glyph < ARRAY_SIZE(cjk_font); glyph++) {
    if (cjk_font[glyph].code == code) {
      return &g1_fake_flash_image[CJK_GLYPH_ADDR + glyph * CJK_GLYPH_BYTES];
    }
  }
  return NULL;
}

static void seed_cjk_font(void)
{
  for (size_t glyph = 0; glyph < ARRAY_SIZE(cjk_font); glyph++) {
    uint8_t *bitmap = &g1_fake_flash_image[CJK_GLYPH_ADDR + glyph * CJK_GLYPH_BYTES];
    uint32_t entry = CJK_INDEX_ADDR + (cjk_font[glyph].code - 0xa4) * 4;

    memset(bitmap, 0, CJK_GLYPH_BYTES);
    for (size_t index = 0; index < ARRAY_SIZE(cjk_font[glyph].strokes); index++) {
      const struct cjk_stroke *rect = &cjk_font[glyph].strokes[index];

      for (int row = rect->row; row < rect->row + rect->height; row++) {
        for (int col = rect->col; col < rect->col + rect->width; col++) {
          bitmap[row * CJK_WIDTH / 2 + col / 2] |=
            (col & 1) ? 0x0f : 0xf0;
        }
      }
    }
    /* the glyph number is the entry's second halfword */
    sys_put_le32((uint32_t)glyph << 16, &g1_fake_flash_image[entry]);
  }
}

static void gui_before(void *fixture)
{
  ARG_UNUSED(fixture);

  gui_test_fb_reset();
  clear_showing_notification_on_gui();
  text_grey_level = 0x55;
  seed_cjk_font();
}

ZTEST_SUITE(gui, NULL, NULL, gui_before, NULL, NULL);
