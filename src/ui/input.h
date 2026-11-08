/*
 * Touch input handling (input.c).
 */
#ifndef G1_UI_INPUT_H_
#define G1_UI_INPUT_H_

#include <stdint.h>
#include "system/glasses_state.h"

extern uint8_t long_press_active;

void check_sw0_status(void);
void input_start(struct GlassesState *state);

#endif /* G1_UI_INPUT_H_ */
