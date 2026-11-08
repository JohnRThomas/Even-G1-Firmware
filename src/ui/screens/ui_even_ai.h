/*
 * Declarations of ui_even_ai.c.
 */
#ifndef G1_UI_SCREENS_UI_EVEN_AI_H_
#define G1_UI_SCREENS_UI_EVEN_AI_H_

#include "ui/screens/ui.h"

extern uint8_t even_ai_scrolling;
int ui_even_ai_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_EVEN_AI_H_ */
