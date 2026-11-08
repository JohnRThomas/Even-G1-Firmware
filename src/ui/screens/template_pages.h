/*
 * Declarations of template_pages.c.
 */
#ifndef G1_UI_SCREENS_TEMPLATE_PAGES_H_
#define G1_UI_SCREENS_TEMPLATE_PAGES_H_

#include "ui/screens/ui.h"

int draw_template_page(struct jbd_panel_context *panel, int idx, void *arg);
void dump_template_gyro_info(void *arg);
int draw_template_gyro_page(struct jbd_panel_context *panel, void *arg);

#endif /* G1_UI_SCREENS_TEMPLATE_PAGES_H_ */
