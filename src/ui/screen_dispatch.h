/*
 * Screen refresh thread (screen_dispatch.c).
 */
#ifndef G1_UI_SCREEN_DISPATCH_H_
#define G1_UI_SCREEN_DISPATCH_H_

#include <stdint.h>
#include <zephyr/kernel.h>
#include "system/glasses_state.h"

extern struct k_mutex screen_payload_mutex; /* guards GlassesState.ui */
extern uint8_t screen_switch_busy;          /* head gesture in progress */
extern uint8_t deferred_text_ack_cmd;       /* text command whose last-packet ack waits for the
					       display refresh (0 = none) */
extern uint8_t deferred_text_ack_idx;
extern uint8_t deferred_text_ack_total;

void screen_dispatch_init(void); /* before any screen runs: the payload mutex */
void screen_dispatch_start(struct GlassesState *state);

#endif /* G1_UI_SCREEN_DISPATCH_H_ */
