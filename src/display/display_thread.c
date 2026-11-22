/*
 * Display thread: the UI framework dispatcher.  Other threads post
 * open / close / input / power messages to display_msgq; the display thread
 * keeps track of the running UI application (screen id) and calls the
 * matching ui_*_task() handler.
 */
#include <glasses_state.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include "../dashboard/dashboard.h"
#include "../gui/gui.h"
#include "../notify/demo_image.h"
#include "../notify/notify.h"
#include "../panel_display/panel.h"
#include "../production_test/production_test.h"
#include "../ui/ui.h"
#include "display_thread.h"
#include "g1_log.h"
#include "ui_event.h"

#define DISPLAY_MSG_MAX_LEN 10

K_MSGQ_DEFINE(display_msgq, sizeof(struct disp_msg), 30, 4);

static K_THREAD_STACK_DEFINE(display_stack, 0x1400);
static struct k_thread display_thread;
static struct k_timer display_timer;

static k_tid_t display_tid;
enum screen_id last_ui_screen_id;
static int display_delay_close_ms;

static int display_wait_ms = 0x42;

static struct disp_state disp_state;
static uint8_t display_prio_boosted;

int set_display_wait_ms(int ms) {
  int wait = ms;

  wait = MIN(wait, 0x7d);
  wait = MAX(wait, 0x21);
  display_wait_ms = wait;
  return 0;
}

int ui_task_handler(struct GlassesState *gs, enum screen_id screen_id, void *ctx, enum ui_event event) {
  struct jbd_panel_context *panel;

  if (get_pt_test_mode() == 1) {
    if (screen_id == SCREEN_DEMO_IMAGE) {
      ui_demo_image_task(&gs->panel, ctx, event);
    }
    return 0;
  }

  panel = &gs->panel;
  last_ui_screen_id = screen_id;

  switch (screen_id) {
    case SCREEN_BITMAP:
      return ui_bitmap_task(panel, ctx, event);
    case SCREEN_TELEPROMPTER:
      return ui_teleprompter_task(panel, ctx, event);
    case SCREEN_DASHBOARD:
      return ui_DashBoard_task(panel, ctx, event);
    case SCREEN_QUICKNOTE:
      return ui_QuickNote_task(panel, ctx, event);
    case SCREEN_TRANSLATE:
      return ui_translate_task(panel, ctx, event);
    case SCREEN_EVEN_AI:
      return ui_even_ai_task(panel, ctx, event);
    case SCREEN_EVEN_AI_V2_INFO:
      return ui_even_ai_v2_info_task(panel, ctx, event);
    case SCREEN_NAVIGATION:
      return ui_navigation_task(panel, ctx, event);
    case SCREEN_NOTIFICATION:
      return ui_ancs_notificaton_task(panel, ctx, event);
    case SCREEN_NEW_MESSAGE:
      return ui_new_message_come_on_task(panel, ctx, event);
    case SCREEN_ONBOARDING:
      return ui_onboarding_task(panel, ctx, event);
    case SCREEN_RASTER_HEIGHT:
      return ui_raster_height_task(panel, ctx, event);
    case SCREEN_SET_IMU_PITCH:
      return ui_set_imu_pitch_task(panel, ctx, event);
    case SCREEN_PROMPT_INFO:
      return ui_prompt_info_task(panel, ctx, event);
    case SCREEN_TRANSCRIBE_INFO:
      return ui_transcribe_info_task(panel, ctx, event);
    default:
      return gs->panel.default_task(&gs->panel, gs->sync.screen_id, &gs->sync);
  }
}

int display_reflash_handler(struct GlassesState *gs, enum screen_id screen_id, void *ctx, enum ui_event event) {
  if (event == UI_EVT_EXIT) {
    if ((screen_id != disp_state.screen_id) && (disp_state.screen_id != SCREEN_IDLE)) {
      G1_LOG_ERR("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      G1_LOG_ERR("The wrong screen ID has appeared, and the exception handling is "
                 "closed,The message ID at startup is %d. The current message ID is%d\n",
                 disp_state.screen_id, screen_id);
      G1_LOG_ERR("To perform exception handling, close the previous SCREEN ID = %d UI "
                 "application first\n",
                 disp_state.screen_id);
      ui_task_handler(gs, disp_state.screen_id, ctx, UI_EVT_EXIT);
      return -1;
    }
  } else if (event == UI_EVT_ENTER) {
    if ((screen_id != disp_state.screen_id) && (disp_state.screen_id != SCREEN_IDLE)) {
      G1_LOG_ERR("------------------------------------------------------------------"
                 "----------------------\n");
      G1_LOG_ERR("There is a screen ID error. The previous screen iD was %d, the "
                 "current screen ID is %d, and the UI process to be performed is %d\n",
                 disp_state.screen_id, screen_id, UI_EVT_ENTER);
      G1_LOG_ERR("The UI framework closes the opened UI application first, and the "
                 "closed application ScreenID is %d\n",
                 disp_state.screen_id);
      ui_task_handler(gs, disp_state.screen_id, ctx, UI_EVT_EXIT);
      G1_LOG_ERR("Because ScreenID has changed, close the old screenID application "
                 "first and update the backup value of screenID to %d\n",
                 screen_id);
      disp_state.screen_id = screen_id;
    }
  } else if (screen_id != disp_state.screen_id) {
    G1_LOG_ERR("******************************************************************"
               "*******************************\n");
    G1_LOG_ERR("There is a screen ID error. The previous screen iD was %d, the "
               "current screen ID is %d, and the UI process to be performed is %d\n",
               disp_state.screen_id, screen_id, (int)event);
  } else {
    /* nothing to do */
  }

  ui_task_handler(gs, screen_id, ctx, event);
  return 0;
}

void display_timer_stop(void) {
  k_timer_stop(&display_timer);
}

int get_display_delay_close_ms(void) {
  return display_delay_close_ms;
}

void display_thread_boost_priority(void) {
  /* set first: the raised thread may run before k_thread_priority_set() returns */
  display_prio_boosted = 1;
  k_thread_priority_set(display_tid, -16);
}

void display_thread_restore_priority(void) {
  if (display_prio_boosted == 1) {
    k_thread_priority_set(display_tid, -14);
    display_prio_boosted = 0;
  }
}

static void display_timer_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);

  display_reflash(NULL, 0);
}

static void display_thread_close(void) {
  jbd_clear_cache_sync(0);
  display_timer_stop();
  memset(&disp_state, 0, sizeof(disp_state));
  display_thread_restore_priority();
}

void display_thread_handler(struct GlassesState *gs) {
  struct disp_msg msg;
  bool received;
  void *ctx = &gs->sync;

  memset(&msg, 0, sizeof(msg));
  k_timer_init(&display_timer, display_timer_expiry, NULL);
  set_frame_buffer(gs->panel.panel_buffer);
  memset(&disp_state, 0, sizeof(disp_state));

  for (;;) {
    if ((gs->work_mode == 1) || (gs->work_mode == 8)) {
      k_sleep(K_MSEC(5000));
      continue;
    }
    memset(&msg, 0, sizeof(msg));
    received = k_msgq_get(&display_msgq, &msg, K_MSEC(display_wait_ms)) == 0;

    switch (disp_state.state) {
      case DISP_STATE_IDLE:
        if (!received) {
          break;
        }
        if (msg.type == DISP_MSG_OPEN) {
          if ((gs->sync.screen_id == SCREEN_EVEN_AI) && (get_dashboard_state()->dash_buf->type < 12)) {
            jbd_clear_cache_sync(0);
          }
          if (msg.len == 0) {
            disp_state.screen_id = gs->sync.screen_id;
            display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_ENTER);
            disp_state.state = DISP_STATE_RUNNING;
          } else if (msg.len == 2) {
            if (msg.data[0] == 1) {
              disp_state.screen_id = (enum screen_id)msg.data[1];
              display_reflash_handler(gs, (enum screen_id)msg.data[1], ctx, UI_EVT_ENTER);
              disp_state.state = DISP_STATE_RUNNING;
            } else {
              G1_LOG_ERR("---screen ID message parampter error!\n");
            }
          } else {
            /* nothing to do */
          }
          display_thread_restore_priority();
        } else if (msg.type == DISP_MSG_CLOSE) {
          memset(&disp_state, 0, sizeof(disp_state));
          if (msg.len == 0) {
            display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_EXIT);
          } else if (msg.len == 2) {
            if (msg.data[0] == 1) {
              display_reflash_handler(gs, (enum screen_id)msg.data[1], ctx, UI_EVT_EXIT);
            } else {
              G1_LOG_ERR("exit .screen ID message parampter error!\n");
              display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_EXIT);
            }
          } else {
            /* nothing to do */
          }
          display_thread_restore_priority();
        } else {
          /* nothing to do */
        }
        break;
      case DISP_STATE_RUNNING:
        if (received) {
          switch (msg.type) {
            case DISP_MSG_OPEN:
              if (msg.len == 0) {
                display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_ENTER);
              } else if (msg.len == 2) {
                if (msg.data[0] == 1) {
                  display_reflash_handler(gs, (enum screen_id)msg.data[1], ctx, UI_EVT_ENTER);
                } else {
                  G1_LOG_ERR("screen ID message parampter error!\n");
                }
              } else {
                /* nothing to do */
              }
              display_thread_restore_priority();
              break;
            case DISP_MSG_CLOSE:
              disp_state.state = DISP_STATE_CLOSING;
              if (msg.len == 0) {
                display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_EXIT);
              } else if (msg.len == 2) {
                if (msg.data[0] == 1) {
                  display_reflash_handler(gs, (enum screen_id)msg.data[1], ctx, UI_EVT_EXIT);
                } else {
                  G1_LOG_ERR("screen ID message parampter error!\n");
                  display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_EXIT);
                }
              } else {
                /* nothing to do */
              }
              display_timer_stop();
              display_thread_close();
              break;
            case DISP_MSG_DELAY_CLOSE:
              display_delay_close_ms = *(int *)msg.data;
              display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_DELAY_CLOSE);
              break;
            case DISP_MSG_INPUT:
              display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_INPUT);
              break;
            case DISP_MSG_MASTER_CLOSE:
              display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_MASTER_CLOSE);
              break;
            case DISP_MSG_LOW_POWER:
              display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_LOW_POWER);
              break;
            case DISP_MSG_POWER_RESUME:
              display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_POWER_RESUME);
              break;
            default:
              break;
          }
        } else if (gs->sync.screen_id != SCREEN_IDLE) {
          display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_REFRESH);
        } else {
          G1_LOG_ERR("When the UI exit command is not received in IDLE state, the UI "
                     "exit is performed by itself\n");
          jbd_clear_cache_sync(0);
          display_timer_stop();
          memset(&disp_state, 0, sizeof(disp_state));
        }
        break;
      case DISP_STATE_CLOSING:
        display_thread_close();
        break;
      default:
        G1_LOG_ERR("unknow display thread state\n");
        display_reflash_handler(gs, gs->sync.screen_id, ctx, UI_EVT_EXIT);
        display_thread_close();
        break;
    }
  }
}

int start_display_thread_handler(struct GlassesState *gs) {
  display_tid = k_thread_create(&display_thread, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
                                (k_thread_entry_t)display_thread_handler, gs, NULL, NULL, -14, 0, K_NO_WAIT);
  return 0;
}

int display_reflash(const void *data, uint32_t len) {
  struct disp_msg msg;

  memset(&msg, 0, sizeof(msg));
  if (len > (uint32_t)DISPLAY_MSG_MAX_LEN) {
    G1_LOG_ERR("send data length more than %d,can't load it,exit ...\n", DISPLAY_MSG_MAX_LEN);
    return -1;
  }
  k_uptime_get();
  msg = (struct disp_msg){.type = DISP_MSG_OPEN};
  if (data != NULL) {
    memcpy(msg.data, data, len);
    msg.len = len;
  }
  if (k_msgq_put(&display_msgq, &msg, K_NO_WAIT) != 0) {
    printk("message queue send failed %s\r\n", __func__);
    return -1;
  }
  k_uptime_get();
  display_thread_boost_priority();
  return 0;
}

int display_close(const void *data, uint32_t len) {
  struct disp_msg msg;

  memset(&msg, 0, sizeof(msg));
  if (len > (uint32_t)DISPLAY_MSG_MAX_LEN) {
    G1_LOG_ERR("send data length more than %d,can't load it,exit ...\n", DISPLAY_MSG_MAX_LEN);
    return -1;
  }
  msg.type = DISP_MSG_CLOSE;
  if (data != NULL) {
    memcpy(msg.data, data, len);
    msg.len = len;
  }
  if (k_msgq_put(&display_msgq, &msg, K_NO_WAIT) != 0) {
    printk("message queue send failed %s\r\n", __func__);
    return -1;
  }
  G1_LOG_ERR("send display close command.......\n");
  return 0;
}

int display_close_screen(enum screen_id screen_id) {
  uint8_t data[2] = {1, 0};

  data[1] = (uint8_t)screen_id;
  if (screen_id != SCREEN_IDLE) {
    G1_LOG_ERR("display_close_screen %d\n", screen_id);
    if (screen_id == SCREEN_NOTIFICATION) {
      set_hide_icon_on_exit();
    }
    display_thread_boost_priority();
    display_close(data, sizeof(data));
    k_yield();
    memset(get_dashboard_state()->ui.buf, 0, sizeof(get_dashboard_state()->ui.buf));
  }
  return 0;
}

int display_DelayClose(int ms) {
  struct disp_msg msg = {.type = DISP_MSG_DELAY_CLOSE, .len = 4};

  *(int *)msg.data = ms;
  if (k_msgq_put(&display_msgq, &msg, K_NO_WAIT) != 0) {
    printk("message queue send failed %s\r\n", __func__);
    return -1;
  }
  return 0;
}

int display_inputEvent(uint8_t a, uint8_t b) {
  struct disp_msg msg = {.type = DISP_MSG_INPUT, .len = 2, .data = {a, b}};

  if (k_msgq_put(&display_msgq, &msg, K_NO_WAIT) != 0) {
    printk("message queue send failed %s\r\n", __func__);
    return -1;
  }
  return 0;
}

int display_MasterSendClose(void) {
  struct disp_msg msg = {.type = DISP_MSG_MASTER_CLOSE};

  if (k_msgq_put(&display_msgq, &msg, K_NO_WAIT) != 0) {
    printk("message queue send failed %s\r\n", __func__);
    return -1;
  }
  return 0;
}

int display_powerEvent(int on) {
  struct disp_msg msg;

  memset(&msg, 0, sizeof(msg));
  if (on == 1) {
    msg.type = DISP_MSG_LOW_POWER;
  } else {
    msg.type = DISP_MSG_POWER_RESUME;
  }
  if (k_msgq_put(&display_msgq, &msg, K_NO_WAIT) != 0) {
    printk("message queue send failed %s\r\n", __func__);
    return -1;
  }
  return 0;
}

int display_reflash_screen(enum screen_id screen_id) {
  uint8_t data[2] = {1, 0};

  data[1] = (uint8_t)screen_id;

  display_reflash(data, sizeof(data));
  k_yield();
  return 0;
}
