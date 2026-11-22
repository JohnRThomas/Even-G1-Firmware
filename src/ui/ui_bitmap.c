/*
 * "Do not disturb" bitmap page (ui_bitmap_task) and the do-not-disturb
 * master/slave synchronisation.
 */
#include <stdint.h>
#include <string.h>
#include <zephyr/toolchain.h>

#include "../gui/gui.h"
#include "../threads/threads.h"
#include "g1_log.h"
#include "glasses_state.h"
#include "ui.h"
#include "ui_event.h"

static uint8_t dnd_bitmap_mode;                       /* do-not-disturb page mode */
static enum ui_page_state bitmap_state;

static void bitmap_state_reset(void) {
  bitmap_state = UI_PAGE_INIT;
}

int ui_bitmap_task(struct jbd_panel_context *panel, void *arg, enum ui_event event) {
  ARG_UNUSED(arg);
  set_frame_buffer(panel->panel_buffer);
  set_showing_notification_on_gui();

  if (event == UI_EVT_EXIT) {
    gui_screen_clear();
    get_dashboard_state()->bitmap_info->enable = 0;
    bitmap_state = UI_PAGE_INIT;
    return 0;
  }
  switch (bitmap_state) {
    case UI_PAGE_INIT: {
      int64_t now;

      gui_screen_clear();
      gui_dynamic_bitmap_reset();
      bitmap_state = UI_PAGE_RUNNING;
      now = k_uptime_get();
      get_dashboard_state()->bitmap_info->deadline = now;
      switch (dnd_bitmap_mode) {
        case 0:
          gui_bmp_dynamic_bitmap_draw(6, gui_screen_x_offset() + 0xc8, gui_screen_y_offset() + 0x3a, 0, 0, 0, 1);
          gui_utf_draw(0, get_string(0x25), 0, gui_screen_x_offset() + 0xf4, gui_screen_y_offset() + 0x37,
                       gui_screen_x_offset() + 0x240, gui_screen_y_offset() + 0x88, 2, 0, 0, NULL, 0);
          break;
        case 2:
          gui_bmp_bitmap_draw(0x49, gui_screen_x_offset(), gui_screen_y_offset() + 0x3a, 0, 0, 0);
          gui_utf_draw(0, get_string(0x26), 0, gui_screen_x_offset() + 0x58, gui_screen_y_offset() + 0x37,
                       gui_screen_x_offset() + 0x240, gui_screen_y_offset() + 0x88, 2, 0, 0, NULL, 0);
          break;
        case 1:
          gui_bmp_dynamic_bitmap_draw(7, gui_screen_x_offset() + 0xd8, gui_screen_y_offset() + 0x3a, 0, 0, 0, 1);
          gui_utf_draw(0, get_string(0x27), 0, gui_screen_x_offset() + 0x104, gui_screen_y_offset() + 0x37,
                       gui_screen_x_offset() + 0x240, gui_screen_y_offset() + 0x88, 2, 0, 0, NULL, 0);
          break;
        default:
          break;
      }
      break;
    }
    case UI_PAGE_RUNNING:
      switch (dnd_bitmap_mode) {
        case 0:
          gui_bmp_dynamic_bitmap_draw(6, gui_screen_x_offset() + 0xc8, gui_screen_y_offset() + 0x3a, 0, 0, 0, 1);
          break;
        case 1:
          gui_bmp_dynamic_bitmap_draw(7, gui_screen_x_offset() + 0xd8, gui_screen_y_offset() + 0x3a, 0, 0, 0, 1);
          break;
        case 2:
          break;
        default:
          get_dashboard_state()->bitmap_info->enable = 0;
          break;
      }
      break;
    default:
      break;
  }
  return 0;
}

static int not_disturb_sync_data(const uint8_t *data, uint8_t len) {
  uint8_t count = len;

  if (data == NULL) {
    return -1;
  }
  if (get_dashboard_state()->is_master == 1) {
    uint8_t buf[4] = {5, 0, 0, 0};

    if (count > 3) {
      count = 3;
    }
    if (count) {
      memcpy(&buf[1], data, count);
    }
    if (sync_to_slave(get_dashboard_state(), SYNC_CMD_SUBCMD, buf, (uint8_t)(count + 1)) > 2999) {
      G1_LOG_ERR("not_disturb SYNC TO Slave failed...\n");
      return -1;
    }
  } else {
    uint8_t buf[2] = {5, 0};

    if (count) {
      count = 1;
      buf[1] = data[0];
    }
    if (sync_to_master(get_dashboard_state(), SLAVE_CMD_SUBCMD, buf, (uint8_t)(count + 1)) >= 2000U) {
      G1_LOG_ERR("not_disturb SYNC TO master failed...\n");
      return -1;
    }
  }
  return 0;
}

void show_not_disturb_bitmap(uint8_t mode) {
  bitmap_state_reset();
  dnd_bitmap_mode = mode;
  get_dashboard_state()->bitmap_info->mode = mode;
  get_dashboard_state()->esb_down_id = dnd_bitmap_mode;
  get_dashboard_state()->bitmap_info->enable = 1;
  get_dashboard_state()->bitmap_info->deadline = k_uptime_get() + 10000;
  update_persist_task_status(get_dashboard_state(), SCREEN_BITMAP, TASK_RUNNING);
}

int sync_not_disturb_bitmap(uint8_t mode) {
  if (get_dashboard_state()->bitmap_info->sync_pending) {
    int ret = not_disturb_sync_data(&mode, 1);

    if (ret != 0) {
      return ret;
    }
    get_dashboard_state()->bitmap_info->sync_pending = ret;
    if (get_dashboard_state()->is_master == 1) {
      show_not_disturb_bitmap(mode);
    }
  }
  return 0;
}

/* request a new do-not-disturb bitmap mode (0..2); synced to the app later */
void request_not_disturb_bitmap(uint8_t mode) {
  if ((mode < 3) && ((get_dashboard_state()->bitmap_info->mode != mode) ||
                     (get_dashboard_state()->dashboard_mode != SCREEN_BITMAP))) {
    get_dashboard_state()->bitmap_info->mode = mode;
    get_dashboard_state()->bitmap_info->sync_pending = 1;
  }
}
