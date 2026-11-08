/*
 * Declarations of ui_translate.c.
 */
#ifndef G1_UI_SCREENS_UI_TRANSLATE_H_
#define G1_UI_SCREENS_UI_TRANSLATE_H_

#include "ui/screens/ui.h"

int draw_template_translate_screen(struct jbd_panel_context *panel, void *arg);
void translate_draw_hint_page(int which);
int ui_translate_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_TRANSLATE_H_ */
