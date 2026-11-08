/*
 * Declarations of ui_transcribe_info.c.
 */
#ifndef G1_UI_SCREENS_UI_TRANSCRIBE_INFO_H_
#define G1_UI_SCREENS_UI_TRANSCRIBE_INFO_H_

#include "ui/screens/ui.h"

void transcribe_info_timeout_reset(void);
int ui_transcribe_info_task(struct jbd_panel_context *ctx, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_TRANSCRIBE_INFO_H_ */
