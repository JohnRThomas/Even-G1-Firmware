/*
 * BLE / SPI request handling of the master (master_process_put_req and its
 * static helpers read_fw_pack_from_nor_flash_by_qspi, process_recv_ble_req).
 */
#include "../ble/ble_ancs.h"
#include "../ble/ble_cmd.h"
#include "../notify/notify.h"
#include "../sensors_audio/sensors_audio.h"
#include "../storage/storage.h"
#include "../ui/ui.h"
#include "../utils/common_utils.h"
#include "../utils/md5.h"
#include "g1_log.h"
#include "glasses_state.h"
#include "threads.h"
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* BLE request header as handed over by the SPI/BLE protocol layer */
struct ble_req {
  uint8_t req_type; /* enum ble_cmd */
  uint8_t sub;
  uint8_t pad_02[2];
  union {
    uint8_t data[0x2b4];
    uint32_t word;
    struct {
      uint32_t w0;
      uint32_t addr;
      uint32_t len;
    } fw;
  };
};

#define BLE_REQ_FW_READ 3

#define BLE_HEAD_UP_ANGLE_MAX 60

static int read_fw_pack_from_nor_flash_by_qspi(uint32_t addr, uint8_t *buf, uint32_t len) {
  int ret;

  if (get_dashboard_state()->flash_read == NULL) {
    return -1;
  }
  ret = get_dashboard_state()->flash_read(get_dashboard_state(), addr, buf, len);
  if (ret != 0) {
    G1_LOG_ERR("read flash fail: %d\n\n", ret);
    return ret;
  }
  return (int)len;
}

static int process_recv_ble_req(struct ble_req *head, uint8_t *out) {
  G1_LOG_ERR("tx error req_type head->req_type %d.\n\n", head->req_type);
  memcpy(out, "error", 6);
  return 6;
}

static void notify_dashboard(struct GlassesState *state, const struct ble_req *head) {
  state->dashboard_cmd = head->req_type;
  k_sem_give(&state->dashboard_position_sem);
}

static int battery_left(struct GlassesState *state) {
  int l;

  if (state->battery_info[0] == 93) {
    l = state->battery_info[2] ? 95 : 94;
  } else if (state->battery_info[0] == 94) {
    l = state->battery_info[2] ? 97 : 96;
  } else if (state->battery_info[0] == 95) {
    l = 98;
  } else if (state->battery_info[0] == 96) {
    l = 99;
  } else if (state->battery_info[0] > 96) {
    l = 100;
  } else {
    l = state->battery_info[0];
  }
  return l;
}

static bool process_fw_read(uint32_t addr, uint32_t total, uint8_t *out) {
  uint32_t n = total - 0x12U;

  if (((total - 0x13U) > 0x800cU) || (n != (uint32_t)read_fw_pack_from_nor_flash_by_qspi(addr, out + 0x12, n))) {
    out[0] = 'n';
    out[1] = 'o';
    return false;
  }
  uint8_t *md5 = out + 2;

  out[0] = 'o';
  out[1] = 'k';
  mbedtls_md5(out + 0x12, n, md5);
  dump_hex("flash_md5", md5, 16);
  return true;
}

int master_process_put_req(struct GlassesState *state, struct ble_req *head, uint8_t *out, uint32_t len) {
  uint8_t *data = head->data;
  enum ble_cmd req;
  uint8_t v;

  if (head->sub == BLE_REQ_FW_READ) {
    uint32_t addr = head->fw.addr;
    uint32_t total = head->fw.len;

    if (!process_fw_read(addr, total, out)) {
      return 0;
    }
    return (int)total;
  }

  req = (enum ble_cmd)head->req_type;
  if ((req >= BLE_CMD_PUT_FIRST) && (req <= BLE_CMD_PUT_LAST)) {
    uint8_t arg;
    uint32_t ts;

    switch (req) {
      case BLE_CMD_BRIGHTNESS_SET:
        if (data[0] > BLE_BRIGHTNESS_LEVEL_MAX) {
          out[0] = BLE_RESP_ERR;
          return 1;
        }
        if ((state->panel.panel_brightness_level != data[0]) || (state->opt3007_context.auto_brightness != data[1])) {
          state->panel.set_brightness_cb(&state->panel, data[0]);
          state->opt3007_context.auto_brightness = data[1];
          notify_dashboard(state, head);
        }
        out[0] = data[0];
        out[1] = data[1];
        out[2] = BLE_RESP_OK;
        return 3;
      case BLE_CMD_ANTI_SHAKE_SET:
        if (state->imu.dof3_enable != data[0]) {
          state->imu.dof3_enable = data[0];
          notify_dashboard(state, head);
        }
        break;
      case BLE_CMD_SILENT_MODE_SET:
        v = data[0];
        arg = data[1];
        if ((v >= (uint8_t)DISPLAY_MODE_NORMAL) && (v <= (uint8_t)DISPLAY_MODE_NOT_DISTURB)) {
          enum display_mode cur = state->display_mode;

          if ((enum display_mode)v == cur) {
            break;
          }
          if (state->is_master == 1) {
            if (cur != DISPLAY_MODE_NOT_DISTURB) {
              if (v == (uint8_t)DISPLAY_MODE_NOT_DISTURB) {
                request_not_disturb_bitmap(0);
              }
            } else if ((v != (uint8_t)DISPLAY_MODE_NOT_DISTURB) && (arg != 1)) {
              show_not_disturb_bitmap(1);
            } else {
              /* nothing to do */
            }
          } else if ((state->is_master == 2) && (cur == DISPLAY_MODE_NOT_DISTURB) &&
                     (v != (uint8_t)DISPLAY_MODE_NOT_DISTURB) && (arg != 1)) {
            show_not_disturb_bitmap(1);
          } else {
            /* nothing to do */
          }
          state->display_mode = (enum display_mode)v;
          notify_dashboard(state, head);
          if (!state->imu_busy && (v == (uint8_t)DISPLAY_MODE_ALWAYS_ON)) {
            state->imu_event = 3;
            k_sem_give(&state->imu_sem);
            return 0;
          }
          break;
        }
        out[0] = BLE_RESP_ERR;
        return 1;
      case BLE_CMD_DEBUG_CONTROL:
        v = data[0];
        arg = data[1];
        if ((enum ble_debug_control_sub)v == DEBUG_CONTROL_SUB_IMU_STATE) {
          trigger_imu_state_change("debug-ble", state, arg != 0);
        } else if ((enum ble_debug_control_sub)v == DEBUG_CONTROL_SUB_LOG_LEVEL) {
          LOG_LEVEL = arg;
          printk("log_level_set=%d", arg);
        } else if ((enum ble_debug_control_sub)v == DEBUG_CONTROL_SUB_DASHBOARD_MODE) {
          state->dashboard_mode = (enum screen_id)arg;
        } else {
          /* nothing to do */
        }
        memcpy(out, "Good", 5);
        return 5;
      case BLE_CMD_COUNTDOWN_TIMER:
        ts = head->word;
        state->countdown_ts->enabled = data[4];
        state->countdown_ts->seconds = ts;
        state->countdown_ts->reset = 1;
        notify_dashboard(state, head);
        out[0] = BLE_RESP_OK;
        if (state->countdown_ts->enabled) {
          update_persist_task_status(state, SCREEN_COUNTDOWN, TASK_RUNNING);
        }
        return 1;
      case BLE_CMD_TELEPROMPTER_INFO:
        memcpy(state->teleprompter, data, sizeof(struct teleprompter_info));
        if (!data[1]) {
          memset(state->teleprompter, 0, sizeof(struct teleprompter_info));
        }
        if (data[1]) {
          update_persist_task_status(state, SCREEN_TELEPROMPTER, TASK_RUNNING);
        }
        return 0;
      case BLE_CMD_TRANSLATE_SRC_TEXT:
        if (dmic_get_stop_state()) {
          return 0;
        }
        state->translate_src->data[TRANSLATE_DATA_LEN - 1] = 0;
        if (!state->translate_src->data[0]) {
          state->translate_src->data[0] = 10;
        }
        out[0] = BLE_RESP_OK;
        if (state->task_status->task != (uint32_t)SCREEN_TRANSLATE) {
          update_persist_task_status(state, SCREEN_TRANSLATE, TASK_RUNNING);
        }
        return 1;
      case BLE_CMD_TRANSLATE_DST_TEXT:
        if (dmic_get_stop_state()) {
          return 0;
        }
        state->translate_dst->data[TRANSLATE_DATA_LEN - 1] = 0;
        if (!state->translate_dst->data[0]) {
          state->translate_dst->data[0] = 10;
        }
        out[0] = BLE_RESP_OK;
        if (state->task_status->task != (uint32_t)SCREEN_TRANSLATE) {
          update_persist_task_status(state, SCREEN_TRANSLATE, TASK_RUNNING);
        }
        return 1;
      case BLE_CMD_HEAD_UP_ACTION_SET:
        memcpy(state->user_sched_info, data, sizeof(struct user_schedule_context));
        *(uint32_t *)state->dashboard_ts = state->user_sched_info->ts;
        accumulate_elapsed_seconds((uint32_t *)state->dashboard_ts, 0, 1);
        notify_dashboard(state, head);
        break;
      case BLE_CMD_HEAD_UP_ANGLE_SET:
        v = data[0];
        arg = data[1];
        if (v > BLE_HEAD_UP_ANGLE_MAX) {
          out[0] = BLE_RESP_ERR;
          return 1;
        }
        if ((state->imu.level != v) || (state->imu.wear != arg)) {
          state->imu.level = v;
          state->imu.wear = arg;
          notify_dashboard(state, head);
        }
        break;
      case BLE_CMD_NOTIFY_EN:
        v = data[0];
        G1_LOG_ERR("BLE_REQ_PUT_NOTIFY_EN enable:%d\n", v);
        set_ancs_enable(v);
        break;
      case BLE_CMD_SHIP_MODE:
        state->dashboard_cmd = 7;
        k_sem_give(&state->dashboard_position_sem);
        return 0;
      case BLE_CMD_HARDWARE_SET:
        if ((enum ble_hw_sub)data[4] != HW_SUB_HEIGHT_DEPTH) {
          return 0;
        }
        G1_LOG_WRN("ble recv raster_config_info:enable is %d, raster_height_gear is %d, canvas_distance_gear is %d\n",
                   data[5], data[6], data[7]);
        if (data[5] != 1) {
          state->raster_height_cfg[0] = 0;
          return 0;
        }
        state->dashboard_ts->dashboard_on = 1;
        state->raster_height_cfg[0] = 1;
        state->panel.raster_height_offset = data[6];
        state->panel.canvas_distance_gear = data[7];
        update_persist_task_status(state, SCREEN_RASTER_HEIGHT, TASK_RUNNING);
        return 0;
      default:
        return 0;
    }
    out[0] = BLE_RESP_OK;
    return 1;
  }
  if ((req >= BLE_CMD_GET_FIRST) && (req <= BLE_CMD_GET_LAST)) {
    switch (req) {
      case BLE_CMD_SILENT_MODE_GET:
        out[0] = (uint8_t)state->display_mode;
        out[1] = (uint8_t)get_wear_state_event();
        return 2;
      case BLE_CMD_ANTI_SHAKE_GET:
        out[0] = state->imu.dof3_enable;
        return 1;
      case BLE_CMD_BRIGHTNESS_GET:
        out[0] = state->panel.panel_brightness_level;
        out[1] = state->opt3007_context.auto_brightness;
        return 2;
      case BLE_CMD_DEVICE_INFO_GET: {
        int l = battery_left(state);
        int r;
        if (state->battery_info[1] == 93) {
          r = 94;
        } else if (state->battery_info[1] == 94) {
          r = 96;
        } else if (state->battery_info[1] == 95) {
          r = 98;
        } else if (state->battery_info[1] == 96) {
          r = 99;
        } else if (state->battery_info[1] > 96) {
          r = 100;
        } else {
          r = state->battery_info[1];
        }
        out[0] = l;
        out[1] = r;
        out[2] = state->battery_voltage;
        out[3] = state->battery_current;
        out[4] = state->battery_temp;
        memcpy(&out[5], &state->master_fw_ver, sizeof(struct fw_ver));
        memcpy(&out[8], &state->fw_ver, sizeof(struct fw_ver));
        memset(&out[11], 0, 8); /* reserved */
        return 0x13;
      }
      case BLE_CMD_MAC_GET:
        memcpy(&out[0], state->master_mac, 6);
        memcpy(&out[6], state->slave_mac, 6);
        return 12;
      case BLE_CMD_HEAD_UP_ANGLE_GET:
        out[0] = state->imu.level;
        out[1] = state->imu.wear;
        return 2;
      case BLE_CMD_DEVICE_SN_GET:
        memcpy(out, get_serial_number(), 16);
        return 16;
      case BLE_CMD_GLASSES_SN_GET:
        memcpy(out, get_glasses_name(), 16);
        return 16;
      case BLE_CMD_ESB_CHANNEL_GET:
        out[0] = state->esb_channel;
        return 1;
      case BLE_CMD_NOTIF_COUNT_GET:
        return get_notification_counts_cmd_response(state, out);
      case BLE_CMD_TIME_SINCE_BOOT_GET:
        uint32_to_little_endian(out, get_boot_seconds());
        out[4] = !state->ble_context.security_pending && (k_sem_count_get(&state->ble_context.tx_sem) != 0U);
        return 5;
      default:
        return 0;
    }
  } else if ((req >= BLE_CMD_POST_FIRST) && (req <= BLE_CMD_POST_LAST)) {
    switch (req) {
      case BLE_CMD_NOTIFICATION_SEND:
        return post_notification_cmd_response(state, (uint8_t *)head, (uint8_t *)&state->sync, (uint8_t *)head, data,
                                              out, len);
      case BLE_CMD_SYNC_DISPLAY:
        v = data[0];
        if (v & 2U) {
          state->esb_hdr.cmd = 3;
          state->esb_hdr.dir = 2;
          state->esb_arg1 = 1;
          uint8_t pkt[0xfc] = {0};

          pkt[0] = 2;
          memcpy(&pkt[1], &state->esb_hdr, 11);
          memset(&pkt[12], 0, 21);
          state->ipc_ctx.send(pkt, sizeof(pkt));
        }
        if ((v & 1U) && state->switch_to_dfu_cb) {
          state->switch_to_dfu_cb(&state->main_sem);
        }
        break;
      case BLE_CMD_TEXT_SET:
        state->dash_buf->data[0x1e6 - 7] = 0;
        if (!state->dash_buf->data[0]) {
          state->dash_buf->data[0] = 10;
        }
        out[0] = BLE_RESP_OK;
        if (state->task_status->task != (uint32_t)SCREEN_EVEN_AI) {
          update_persist_task_status(state, SCREEN_EVEN_AI, TASK_RUNNING);
        }
        return 1;
      case BLE_CMD_NOTIFICATION_CLEAR:
        ancs_delete_notification(__builtin_bswap32(head->word));
        break;
      case BLE_CMD_MTU_SET:
        k_sem_give(&get_dashboard_state()->ble_context.tx_sem);
        break;
      case BLE_CMD_NOTIF_AUTO_DISPLAY_SET:
        get_dashboard_state()->message_mode = data[0];
        get_dashboard_state()->message_stay_time = data[1];
        get_dashboard_state()->dashboard_cmd = head->req_type;
        k_sem_give(&get_dashboard_state()->dashboard_position_sem);
        out[0] = BLE_RESP_OK;
        out[1] = data[0];
        return 2;
      default:
        return 0;
    }
    out[0] = BLE_RESP_OK;
    return 1;
  } else {
    /* nothing to do */
  }
  return process_recv_ble_req(head, out);
}
