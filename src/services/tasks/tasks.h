/*
 * Task status (task_status.c) and task control (task_control.c): what the glasses
 * are doing (persistent task, temporary task, idle) and how a task is left.
 */
#ifndef G1_SERVICES_TASKS_TASKS_H_
#define G1_SERVICES_TASKS_TASKS_H_

#include <stdint.h>
#include <stdbool.h>
#include "system/glasses_state.h"

/* task_status.c */
extern uint8_t task_status_lock;
uint8_t is_persist_task_active(struct GlassesState *state, int mode);
int is_task_running(struct GlassesState *state);
int update_persist_task_status(struct GlassesState *state, int task, int status);
int update_temp_task_status(struct GlassesState *state, int task, int status);
int update_persist_task_status_to_idle(struct GlassesState *state);
int update_persist_task_status_to_wait_blow_head(struct GlassesState *state);
bool task_uses_mic(int task);
bool is_idle(void);

/* task_control.c */
void exit_running_task(struct GlassesState *state, int notify);
void exit_running_task_keep_status(struct GlassesState *state, int notify);
void exit_temp_task(struct GlassesState *state, int flag);

#endif /* G1_SERVICES_TASKS_TASKS_H_ */
