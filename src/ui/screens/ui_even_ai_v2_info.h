/*
 * Declarations of ui_even_ai_v2_info.c.
 */
#ifndef G1_UI_SCREENS_UI_EVEN_AI_V2_INFO_H_
#define G1_UI_SCREENS_UI_EVEN_AI_V2_INFO_H_

#include "ui/screens/ui.h"

void even_ai_v2_info_timeout_reset(void);
int ui_even_ai_v2_info_task(struct jbd_panel_context *ctx, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_EVEN_AI_V2_INFO_H_ */
