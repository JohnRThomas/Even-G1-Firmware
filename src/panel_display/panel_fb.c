/*
 * Panel frame buffer helpers: brightness curve, draw / clear callbacks and
 * frame buffer copies, on top of the panel context (get_jdb_panel_context()).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "g1_log.h"
#include "glasses_state.h"
#include "panel.h"

/* the current register (46h) is 6 bits wide */
#define JBD_CUR_REG_MAX 63

int clean_fb_data(uint8_t **fb, int val, int x_start, int y_start, int x_end, int y_end) {
  int y;
  int x_last = x_end;
  int y_last = y_end;

  if (x_last >= PANEL_WIDTH) {
    x_last = PANEL_WIDTH - 1;
  }
  if (y_last >= PANEL_FB_ROWS) {
    y_last = PANEL_FB_ROWS - 1;
  }
  if ((x_start > x_last) || (y_start > y_last)) {
    G1_LOG_ERR("clean frame buffer parameter error!!!\n");
    return -1;
  }
  for (y = y_start; y <= y_last; y++) {
    memset(fb[y] + (x_start / 2), val, (x_last / 2) - (x_start / 2) + 1);
  }
  return (x_last / 2) - (x_start / 2) + 1;
}

void update_brightness_level_for_jdb_panel(uint8_t level, uint16_t *lum, uint8_t *cur) {
  if (level <= 5) {
    int base = get_dashboard_state()->panel.brightness_max * 2 / 9;

    if ((base == 0) || (get_dashboard_state()->panel.brightness_max <= 5)) {
      *lum = ((level * 111 / 6) + 1) * (get_dashboard_state()->panel.brightness_max + 1);
    } else {
      *lum = (base + (level * (get_dashboard_state()->panel.brightness_max - base) / 6)) * 111;
    }
    *cur = 0;
  } else {
    *lum = (get_dashboard_state()->panel.brightness_max + 1) * 111;
    *cur = MIN(level - 6, JBD_CUR_REG_MAX);
  }
  if (((unsigned int)(get_dashboard_state()->panel.brightness_percent > 69) &
       (unsigned int)(get_dashboard_state()->panel.brightness_percent < 100)) != 0U) {
    *lum = *lum * get_dashboard_state()->panel.brightness_percent / 100;
  }
  if (*lum == 0) {
    *lum = 1;
  }
}

void *clear_panel(struct jbd_panel_context *c) {
  return memset(c->panel_buffer[0], 0, c->current_buffer_size);
}

void panel_draw_cb(struct jbd_panel_context *c) {
  if (c->init_done != 0U) {
    display_image(c->current_row, c->current_column, c->panel_buffer[0], (int)c->current_buffer_size);
  }
}

struct jbd_panel_context *get_jdb_panel_context(void) {
  return &get_dashboard_state()->panel;
}

int copy_to_frame_buffer_raw(uint8_t **fb, const uint8_t *src, int w, int h, int x, int y) {
  int i;

  for (i = 0; i < h; i++) {
    memcpy(fb[y + i] + (x % PANEL_FB_STRIDE), src + (i * w), w);
  }
  return y;
}

int copy_to_frame_buffer(uint8_t **fb, const uint8_t *src, int w, int h, int x, int y) {
  int i;
  int dst_x = x;
  int dst_y = y;

  if (dst_y >= PANEL_FB_ROWS) {
    dst_y = PANEL_FB_ROWS;
  }
  if (dst_x >= PANEL_WIDTH) {
    dst_x = PANEL_WIDTH;
  }
  for (i = 0; (i < h) && (dst_y + i) < PANEL_FB_ROWS; i++) {
    memcpy(fb[dst_y + i] + (dst_x / 2), src + (i * w), MIN(w, PANEL_FB_STRIDE - dst_x / 2));
  }
  return dst_y;
}

int or_to_frame_buffer(uint8_t **fb, const uint8_t *src, int w, int h, int x, int y) {
  int k = 0;
  int i;
  int j;
  int x_byte;
  int dst_x = x;
  int dst_y = y;

  if (dst_y >= PANEL_FB_ROWS) {
    dst_y = PANEL_FB_ROWS;
  }
  if (dst_x >= PANEL_WIDTH) {
    dst_x = PANEL_WIDTH;
  }
  x_byte = dst_x / 2;
  for (i = 0; (i < h) && (dst_y + i) < PANEL_FB_ROWS; i++) {
    for (j = 0; j < (w / 2); j++) {
      if ((x_byte + j) < PANEL_FB_STRIDE) {
        fb[dst_y + i][x_byte + j] |= src[k];
      }
      k++;
    }
  }
  return dst_y;
}

void copy_frame_buffer_rows(uint8_t **fb, int dst_x, int dst_y, int src_x, int src_y, int h, int w) {
  int i;

  for (i = 0; i < h; i++) {
    /* moving down, the last row goes first: overlapping rows are read before written */
    int r = (dst_y > src_y) ? (h - 1 - i) : i;

    memcpy(fb[dst_y + r] + (dst_x / 2), fb[src_y + r] + (src_x / 2), w / 2);
  }
}
