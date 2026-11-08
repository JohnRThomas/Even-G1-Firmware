/*
 * Declarations of ui_quicknote.c.
 */
#ifndef G1_UI_SCREENS_UI_QUICKNOTE_H_
#define G1_UI_SCREENS_UI_QUICKNOTE_H_

#include "ui/screens/ui.h"

extern uint8_t quicknote_result_shown;
void dmic_record_start(void);
void quicknote_stop_record(void);
void quicknote_timeout_reset(void);
int ui_QuickNote_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_QUICKNOTE_H_ */
