/* Weak fakes (threads.c overrides some); sys_reboot() fails the test. */
#ifndef G1_UNIT_FAKES_NFC_H_
#define G1_UNIT_FAKES_NFC_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

#include "harness.h"

DECLARE_FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
DECLARE_FAKE_VOID_FUNC(update_activity_time);
DECLARE_FAKE_VOID_FUNC(enter_silent_mode);
DECLARE_FAKE_VOID_FUNC(exit_silent_mode);
DECLARE_FAKE_VALUE_FUNC(const struct device *, get_charger_device);
extern uint8_t can_enter_low_power;

DECLARE_FAKE_VOID_FUNC(set_ncs_mem_pt_level, uint8_t);
extern int esb_signal;

DECLARE_FAKE_VOID_FUNC(pt_comm_busy_set);
DECLARE_FAKE_VOID_FUNC(pt_comm_busy_clear);
DECLARE_FAKE_VOID_FUNC(aw9320x_diff_get, int32_t *);

DECLARE_FAKE_VALUE_FUNC(int, calc_flash_crc, uint16_t *, int, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, save_sys_settings, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, save_usr_settings, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, read_sys_settting_from_flash, struct sys_settings *);
DECLARE_FAKE_VALUE_FUNC(int, read_usr_settting_from_flash, struct usr_settings *);
DECLARE_FAKE_VALUE_FUNC(char *, get_serial_number);
DECLARE_FAKE_VALUE_FUNC(char *, get_glasses_name);
DECLARE_FAKE_VALUE_FUNC(int, save_serial_number, const char *);
DECLARE_FAKE_VALUE_FUNC(int, dump_serial_number, const char *);

DECLARE_FAKE_VALUE_FUNC(int, enable_power_for_panel);
DECLARE_FAKE_VALUE_FUNC(int, disable_power_for_panel);
DECLARE_FAKE_VALUE_FUNC(int, opt3007_read, int);
DECLARE_FAKE_VALUE_FUNC(int, erase_audio_buffer);
DECLARE_FAKE_VALUE_FUNC(int, request_dmic_stream_start);
/* dmic_stream_stop() is only in fakes_nfc.c: its two prototypes disagree */

DECLARE_FAKE_VALUE_FUNC(int, lpuart_async_b, const struct device *, uint8_t *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, read_serial_data, const struct device *, uint8_t **, uint32_t *);
DECLARE_FAKE_VOID_FUNC(serial_rx_clear);

DECLARE_FAKE_VOID_FUNC(demo_image_select, uint8_t);
extern uint8_t demo_mode;
extern uint32_t imu_roll_x100;
extern uint32_t imu_yaw_x100;

DECLARE_FAKE_VALUE_FUNC(int, enqueue_bt_data, void *, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, get_work_mode);
DECLARE_FAKE_VALUE_FUNC(int, is_task_running, struct GlassesState *);
DECLARE_FAKE_VALUE_FUNC(int, update_persist_task_status_to_idle, struct GlassesState *);
DECLARE_FAKE_VOID_FUNC(aw93203_init);
DECLARE_FAKE_VALUE_FUNC(int32_t, aw9320x_read_chipid);
DECLARE_FAKE_VALUE_FUNC(int, display_close_screen, int);
DECLARE_FAKE_VALUE_FUNC(int, display_reflash_screen, uint8_t);
DECLARE_FAKE_VOID_FUNC(enter_wear_burial_point);
DECLARE_FAKE_VOID_FUNC(exit_wear_burial_point);
DECLARE_FAKE_VOID_FUNC(free_demo_line_buf);
DECLARE_FAKE_VALUE_FUNC(int, global_system_resume, struct GlassesState *);
DECLARE_FAKE_VALUE_FUNC(int, global_system_suspend, struct GlassesState *);
DECLARE_FAKE_VALUE_FUNC(int, init_serial_comm);
DECLARE_FAKE_VALUE_FUNC(uint8_t, opt3007_get_brightness_level, struct opt3007_context *);
DECLARE_FAKE_VOID_FUNC(reset_dash_buf, int);
DECLARE_FAKE_VALUE_FUNC(uint32_t, set_date, uint32_t);
extern uint8_t task_status_lock;

extern struct g1_pkt_log g1_nfc_bt_log;

#endif /* G1_UNIT_FAKES_NFC_H_ */
