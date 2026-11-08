/*
 * Declarations of panel_fb.c.
 */
#ifndef G1_DRIVERS_PANEL_PANEL_FB_H_
#define G1_DRIVERS_PANEL_PANEL_FB_H_

#include "drivers/display/panel.h"

int clean_fb_data(uint8_t **fb, int val, int x_start, int y_start, int x_end, int y_end);
void update_brightness_level_for_jdb_panel(uint8_t level, uint16_t *lum, uint8_t *cur);
void *clear_panel(struct jbd_panel_context *c);
void panel_draw_cb(struct jbd_panel_context *c);
struct jbd_panel_context *get_jdb_panel_context(void);
int copy_to_frame_buffer_raw(uint8_t **fb, const uint8_t *src, int w, int h, int x, int y);
int copy_to_frame_buffer(uint8_t **fb, const uint8_t *src, int w, int h, int x, int y);
int or_to_frame_buffer(uint8_t **fb, const uint8_t *src, int w, int h, int x, int y);
void copy_frame_buffer_rows(uint8_t **fb, int dst_x, int dst_y, int src_x, int src_y, int h, int w);

#endif /* G1_DRIVERS_PANEL_PANEL_FB_H_ */
