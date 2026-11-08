/*
 * Screen task state machine (screen_task.c): which screen runs for the current
 * task, the transitions between them, and the message (notification) key handling.
 */
#ifndef G1_UI_SCREEN_TASK_H_
#define G1_UI_SCREEN_TASK_H_

#include <stdint.h>
#include <zephyr/sys/atomic.h>
#include "system/glasses_state.h"

extern uint8_t msg_busy;
extern uint8_t new_msg_signal_received;
extern atomic_t dashboard_flags;
extern atomic_t close_app_flags;

void set_msg_timeout_flag(void);
uint8_t take_msg_timeout_flag(void);
int sync_message_signal_to_slave(uint8_t signal);
int send_msg_key_to_master(void);
void handle_msg_touch_key(void);
int screen_task_dispatch(struct GlassesState *state, struct imu_ctx *ctx);
void reset_dash_buf(int clear);
void screen_task_start(struct GlassesState *state);

#endif /* G1_UI_SCREEN_TASK_H_ */
