/*
 * Declarations of ui_new_message.c.
 */
#ifndef G1_UI_SCREENS_UI_NEW_MESSAGE_H_
#define G1_UI_SCREENS_UI_NEW_MESSAGE_H_

#include "ui/screens/ui.h"

int ui_new_message_come_on_task(struct jbd_panel_context *panel, void *arg, int event);
int draw_template_new_message_page(struct jbd_panel_context *panel, void *arg);

#endif /* G1_UI_SCREENS_UI_NEW_MESSAGE_H_ */
