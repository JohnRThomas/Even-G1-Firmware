/*
 * Declarations of ui_teleprompter.c.
 */
#ifndef G1_UI_SCREENS_UI_TELEPROMPTER_H_
#define G1_UI_SCREENS_UI_TELEPROMPTER_H_

#include "ui/screens/ui.h"

/* set by the BLE PUT handlers */
extern int tp_app_sync_count;     /* app sync request seen */
extern uint8_t tp_scroll_pending; /* teleprompter text scrolled */

void teleprompter_state_reset(void);
int ui_teleprompter_task(struct jbd_panel_context *panel, void *arg, int event);
int draw_template_teleprompter_page(struct jbd_panel_context *panel, void *arg);

#endif /* G1_UI_SCREENS_UI_TELEPROMPTER_H_ */
