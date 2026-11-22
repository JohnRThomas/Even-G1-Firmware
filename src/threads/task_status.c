/*
 * Persist / temporary task status.  The screen on/off and suspend/resume
 * part is in system_state.c, the screen / message state machine in
 * message_task.c.
 */
#include "../display/display_thread.h"
#include "g1_log.h"
#include "threads.h"
#include <glasses_state.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>

static uint8_t saved_auto_brightness;
uint8_t task_status_lock;

uint8_t is_persist_task_active(struct GlassesState *state, enum screen_id mode) {
  ARG_UNUSED(mode);

  while (task_status_lock) {
    k_msleep(1);
  }
  return state->task_status->status > TASK_STOP;
}

int is_task_running(const struct GlassesState *state) {
  while (task_status_lock) {
    k_msleep(1);
  }
  return state->dashboard_mode > SCREEN_WAIT_BLOW_HEAD;
}

enum task_status update_persist_task_status(struct GlassesState *state, enum screen_id task, enum task_status status) {
  while (task_status_lock) {
    k_msleep(1);
  }
  task_status_lock = 1;
  if ((state->task_status->task == (uint32_t)task) &&
      ((state->task_status->status == TASK_STOP) || (state->task_status->status == status))) {
    task_status_lock = 0;
    return status;
  }
  if ((state->dashboard_mode != SCREEN_IDLE) && (state->dashboard_mode != SCREEN_WAIT_BLOW_HEAD)) {
    if (!state->dashboard_ts->exit_pending || (state->dashboard_mode != SCREEN_DASHBOARD)) {
      if (status == TASK_RUNNING) {
        switch (task) {
          case SCREEN_NOTIFICATION:
          case SCREEN_DASHBOARD:
          case SCREEN_BITMAP:
          case SCREEN_EVEN_AI:
          case SCREEN_EVEN_AI_V2_INFO:
            break;
          default:
            task_status_lock = 0;
            return status;
        }
      }
      display_close_screen(state->dashboard_mode);
      state->dashboard_mode = task;
    }
  } else {
    state->dashboard_mode = task;
  }
  if (status == TASK_RUNNING) {
    G1_LOG_ERR("persist task [running]: %d\n", task);
    if (task == SCREEN_EVEN_AI) {
      saved_auto_brightness = state->opt3007_context.auto_brightness;
      if (state->opt3007_context.auto_brightness == 1) {
        state->opt3007_context.auto_brightness = 0;
      }
    }
    if (state->evenai_buf->state > 1) {
      state->evenai_buf->state = 0;
    }
  } else if (status == TASK_PAUSE) {
    G1_LOG_ERR("persist task [pause]: %d\n", task);
  } else {
    G1_LOG_ERR("persist task [stop]: %d\n", task);
  }
  state->task_status->task = (uint32_t)task;
  state->task_status->status = status;
  task_status_lock = 0;
  return status;
}

enum task_status update_temp_task_status(struct GlassesState *state, enum screen_id task, enum task_status status) {
  while (task_status_lock) {
    k_msleep(1);
  }
  task_status_lock = 1;
  if (((state->task_status->task == (uint32_t)task) &&
       ((state->task_status->status == TASK_STOP) || (state->task_status->status == status))) ||
      (state->dashboard_mode == task)) {
    task_status_lock = 0;
    return status;
  }
  if ((state->dashboard_mode != SCREEN_IDLE) && (state->dashboard_mode != SCREEN_WAIT_BLOW_HEAD)) {
    if ((status == TASK_RUNNING) && (task != SCREEN_NOTIFICATION) && (task != SCREEN_DASHBOARD)) {
      task_status_lock = 0;
      return status;
    }
    display_close_screen(state->dashboard_mode);
  }
  if (status == TASK_RUNNING) {
    G1_LOG_ERR("persist task [running]: %d\n", task);
  } else if (status == TASK_PAUSE) {
    G1_LOG_ERR("persist task [pause]: %d\n", task);
  } else {
    G1_LOG_ERR("persist task [stop]: %d\n", task);
  }
  state->dashboard_mode = task;
  state->task_status->task = (uint32_t)SCREEN_IDLE;
  state->task_status->status = TASK_STOP;
  task_status_lock = 0;
  return status;
}

int update_persist_task_status_to_idle(struct GlassesState *state) {
  while (task_status_lock) {
    k_msleep(1);
  }
  task_status_lock = 1;
  if ((state->dashboard_mode == SCREEN_IDLE) && (state->task_status->status == TASK_STOP)) {
    task_status_lock = 0;
    return 0;
  }
  if ((state->dashboard_mode != SCREEN_IDLE) && (state->dashboard_mode != SCREEN_WAIT_BLOW_HEAD)) {
    display_close_screen(state->dashboard_mode);
  }
  if (state->dashboard_mode == SCREEN_EVEN_AI) {
    state->opt3007_context.auto_brightness = saved_auto_brightness;
  }
  G1_LOG_ERR("persist task [stop]: %d\n", SCREEN_IDLE);
  state->dashboard_mode = SCREEN_IDLE;
  state->task_status->status = TASK_STOP;
  task_status_lock = 0;
  return 0;
}

int update_persist_task_status_to_wait_blow_head(struct GlassesState *state) {
  while (task_status_lock) {
    k_msleep(1);
  }
  task_status_lock = 1;
  if ((state->dashboard_mode != SCREEN_IDLE) && (state->dashboard_mode != SCREEN_WAIT_BLOW_HEAD)) {
    display_close_screen(state->dashboard_mode);
  }
  G1_LOG_ERR("persist task [stop]: %d\n", SCREEN_WAIT_BLOW_HEAD);
  state->dashboard_mode = SCREEN_WAIT_BLOW_HEAD;
  state->task_status->task = (uint32_t)SCREEN_WAIT_BLOW_HEAD;
  state->task_status->status = TASK_STOP;
  task_status_lock = 0;
  return 0;
}

bool task_uses_mic(enum screen_id task) {
  switch (task) {
    case SCREEN_TELEPROMPTER:
    case SCREEN_TRANSLATE:
    case SCREEN_QUICKNOTE:
    case SCREEN_EVEN_AI:
    case SCREEN_TRANSCRIBE_INFO:
    case SCREEN_EVEN_AI_V2_INFO:
      return true;
    default:
      return false;
  }
}

bool is_idle(void) {
  if (get_dashboard_state()->task_status->task != (uint32_t)SCREEN_IDLE) {
    return false;
  }
  if (get_dashboard_state()->task_status->status != TASK_STOP) {
    return false;
  }
  if (get_dashboard_state()->dashboard_mode != SCREEN_IDLE) {
    return false;
  }
  return get_dashboard_state()->suspend_state == 1;
}
