/* Production test, thread, main.c, sensor and panel fakes. */
#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <zephyr/random/rand32.h>

#include "fakes.h"

#include "aw9320x/aw9320x.h"
#include "display/display_thread.h"
#include "gui/gui.h"
#include "imu/imu_fusion.h"
#include "main.h"
#include "nfc/nfc_wlc.h"
#include "nfc/st25dv.h"
#include "notify/demo_image.h"
#include "notify/notify.h"
#include "panel_display/panel.h"
#include "production_test/production_test.h"
#include "sensors_audio/sensors_audio.h"
#include "storage/storage.h"
#include "threads/threads.h"
#include "ui/ui.h"

DEFINE_FAKE_VALUE_FUNC(uint8_t, get_pt_level);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_pt_level_flag);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_pt_test_mode);
DEFINE_FAKE_VOID_FUNC(set_test_mode, uint8_t);

DEFINE_FAKE_VOID_FUNC(system_reboot_delayed);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_wear_state_event);
DEFINE_FAKE_VALUE_FUNC(int, trigger_imu_state_change, void *, struct GlassesState *, bool);
DEFINE_FAKE_VOID_FUNC(report_event_to_phone, int);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_dashboard_startup_mode);
DEFINE_FAKE_VALUE_FUNC(uint8_t, cur_message_show_state);
DEFINE_FAKE_VALUE_FUNC(int, display_close_screen, int);
__weak uint8_t last_ble_cmd = 0xff;
__weak uint8_t msg_array_busy;
__weak uint8_t deferred_text_ack_cmd;
__weak uint8_t deferred_text_ack_idx;
__weak uint8_t deferred_text_ack_total;
__weak atomic_t dashboard_flags;

FAKE_VALUE_FUNC(uint8_t, is_master);
DEFINE_FAKE_VALUE_FUNC(int, change_work_mode, int);
DEFINE_FAKE_VOID_FUNC(setEraseDFU, bool);
DEFINE_FAKE_VOID_FUNC(deinit_ncs_memory);
DEFINE_FAKE_VOID_FUNC(save_dashboard_ts_to_ncs_mem);
FAKE_VOID_FUNC(set_ncs_mem_notes_index, uint8_t);
DEFINE_FAKE_VOID_FUNC(set_ncs_mem_initialized);
DEFINE_FAKE_VALUE_FUNC(int, set_shipmode, int);
__weak char net_build_time[32];

FAKE_VALUE_FUNC(int, opt3007_get_max_lux);
DEFINE_FAKE_VALUE_FUNC(int, is_in_box);
FAKE_VALUE_FUNC(uint32_t, chargerGetVdcRect);
__weak uint32_t imu_pitch_x100;
__weak atomic_t dashboard_head_up_opened;

DEFINE_FAKE_VOID_FUNC(set_brightness_to_panel_reg_in_running, uint8_t);
__weak uint32_t JDB_PANEL_ID;

/* 0: get_string() falls back to English */
FAKE_VALUE_FUNC(uint8_t, get_current_language);
/* harness reset zeroes it */
__weak volatile struct app_setting CURRENT_LANGUAGE;

DEFINE_FAKE_VALUE_FUNC(uint32_t, z_impl_sys_rand32_get);

DEFINE_FAKE_VALUE_FUNC(int, g1_fake_panel_set_brightness, struct jbd_panel_context *, uint8_t);

DEFINE_FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
FAKE_VOID_FUNC(update_activity_time);
FAKE_VOID_FUNC(enter_silent_mode);
FAKE_VOID_FUNC(exit_silent_mode);
FAKE_VALUE_FUNC(const struct device *, get_charger_device);
__weak uint8_t can_enter_low_power;
DEFINE_FAKE_VOID_FUNC(set_ncs_mem_pt_level, uint8_t);
DEFINE_FAKE_VOID_FUNC(pt_comm_busy_set);
DEFINE_FAKE_VOID_FUNC(pt_comm_busy_clear);
FAKE_VOID_FUNC(aw9320x_diff_get, int32_t *);
FAKE_VALUE_FUNC(int, calc_flash_crc, uint16_t *, int, uint32_t);
FAKE_VALUE_FUNC(int, save_sys_settings, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, save_usr_settings, struct GlassesState *, int);
FAKE_VALUE_FUNC(int, read_sys_settting_from_flash, struct sys_settings *);
FAKE_VALUE_FUNC(int, read_usr_settting_from_flash, struct usr_settings *);
FAKE_VALUE_FUNC(char *, get_serial_number);
DEFINE_FAKE_VALUE_FUNC(char *, get_glasses_name);
FAKE_VALUE_FUNC(int, save_serial_number, const char *);
FAKE_VALUE_FUNC(int, dump_serial_number, const char *);
FAKE_VALUE_FUNC(int, enable_power_for_panel);
FAKE_VALUE_FUNC(int, disable_power_for_panel);
FAKE_VALUE_FUNC(int, opt3007_read, int);
FAKE_VALUE_FUNC(int, erase_audio_buffer);
FAKE_VOID_FUNC(demo_image_select, uint8_t);
__weak uint8_t demo_mode;
__weak uint32_t imu_roll_x100;
__weak uint32_t imu_yaw_x100;

static uint8_t is_master_default(void)
{
  return get_dashboard_state()->is_master;
}

static int panel_set_brightness_default(struct jbd_panel_context *panel, uint8_t level)
{
  if (panel->panel_brightness_level != level) {
    panel->panel_brightness_level = level;
  }
  return level;
}

static void fakes_misc_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);

  RESET_FAKE(get_pt_level);
  RESET_FAKE(get_pt_level_flag);
  RESET_FAKE(get_pt_test_mode);
  RESET_FAKE(set_test_mode);

  RESET_FAKE(system_reboot_delayed);
  RESET_FAKE(get_wear_state_event);
  RESET_FAKE(trigger_imu_state_change);
  RESET_FAKE(report_event_to_phone);
  RESET_FAKE(get_dashboard_startup_mode);
  RESET_FAKE(cur_message_show_state);
  RESET_FAKE(display_close_screen);
  last_ble_cmd = 0xff;
  msg_array_busy = 0;
  deferred_text_ack_cmd = 0;
  deferred_text_ack_idx = 0;
  deferred_text_ack_total = 0;
  atomic_set(&dashboard_flags, 0);

  RESET_FAKE(is_master);
  is_master_fake.custom_fake = is_master_default;
  RESET_FAKE(change_work_mode);
  RESET_FAKE(setEraseDFU);
  RESET_FAKE(deinit_ncs_memory);
  RESET_FAKE(save_dashboard_ts_to_ncs_mem);
  RESET_FAKE(set_ncs_mem_initialized);
  RESET_FAKE(set_shipmode);
  memset(net_build_time, 0, sizeof(net_build_time));

  RESET_FAKE(is_in_box);
  imu_pitch_x100 = 0;
  atomic_set(&dashboard_head_up_opened, 0);

  RESET_FAKE(set_brightness_to_panel_reg_in_running);
  JDB_PANEL_ID = 0;

  RESET_FAKE(z_impl_sys_rand32_get);

  RESET_FAKE(g1_fake_panel_set_brightness);
  g1_fake_panel_set_brightness_fake.custom_fake = panel_set_brightness_default;

  RESET_FAKE(nfc_deadline_set);
  can_enter_low_power = 0;
  RESET_FAKE(set_ncs_mem_pt_level);
  RESET_FAKE(pt_comm_busy_set);
  RESET_FAKE(pt_comm_busy_clear);
  RESET_FAKE(save_usr_settings);
  RESET_FAKE(get_glasses_name);
  demo_mode = 0;
  imu_roll_x100 = 0;
  imu_yaw_x100 = 0;

  /* as init_context() registers it */
  get_dashboard_state()->panel.set_brightness_cb = g1_fake_panel_set_brightness;
}

G1_UNIT_RESET_HOOK(fakes_misc_reset);
