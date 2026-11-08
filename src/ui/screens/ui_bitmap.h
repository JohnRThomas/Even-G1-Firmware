/*
 * Declarations of ui_bitmap.c.
 */
#ifndef G1_UI_SCREENS_UI_BITMAP_H_
#define G1_UI_SCREENS_UI_BITMAP_H_

#include "ui/screens/ui.h"

int draw_template_bitmap_page(struct jbd_panel_context *panel, void *arg);
int ui_bitmap_task(struct jbd_panel_context *panel, void *arg, int event);
void show_not_disturb_bitmap(uint8_t mode);
int sync_not_disturb_bitmap(uint8_t mode);
void request_not_disturb_bitmap(uint8_t mode);

#endif /* G1_UI_SCREENS_UI_BITMAP_H_ */
