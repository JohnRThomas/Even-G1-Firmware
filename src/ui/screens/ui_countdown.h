/*
 * Declarations of ui_countdown.c.
 */
#ifndef G1_UI_SCREENS_UI_COUNTDOWN_H_
#define G1_UI_SCREENS_UI_COUNTDOWN_H_

#include "ui/screens/ui.h"
#include "services/notifications/notify.h"

void draw_countdown_running_status(uint8_t **fb, void *arg, int a, int b, int c);
void countdown_tick(struct countdown_ts_context *cd);
int draw_template_countdown_page(struct jbd_panel_context *panel, void *arg);
void countdown_reset(struct countdown_ts_context *cd);

#endif /* G1_UI_SCREENS_UI_COUNTDOWN_H_ */
