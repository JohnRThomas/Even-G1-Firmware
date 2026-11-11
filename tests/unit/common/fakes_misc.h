/* Production test, thread, main.c, sensor, panel and string fakes. */
#ifndef G1_UNIT_FAKES_MISC_H_
#define G1_UNIT_FAKES_MISC_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

DECLARE_FAKE_VALUE_FUNC(uint8_t, get_pt_level);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_pt_level_flag);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_pt_test_mode);
DECLARE_FAKE_VOID_FUNC(set_test_mode, uint8_t);

DECLARE_FAKE_VOID_FUNC(system_reboot_delayed);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_wear_state_event);
DECLARE_FAKE_VALUE_FUNC(int, trigger_imu_state_change, void *, struct GlassesState *, bool);
extern uint8_t last_ble_cmd;
extern uint8_t msg_array_busy;
extern uint8_t deferred_text_ack_cmd;
extern uint8_t deferred_text_ack_idx;
extern uint8_t deferred_text_ack_total;

DECLARE_FAKE_VALUE_FUNC(uint8_t, is_master);
DECLARE_FAKE_VALUE_FUNC(int, change_work_mode, int);
DECLARE_FAKE_VOID_FUNC(setEraseDFU, bool);
DECLARE_FAKE_VOID_FUNC(deinit_ncs_memory);
DECLARE_FAKE_VOID_FUNC(save_dashboard_ts_to_ncs_mem);
DECLARE_FAKE_VOID_FUNC(set_ncs_mem_initialized);
DECLARE_FAKE_VALUE_FUNC(int, set_shipmode, int);
extern char net_build_time[32];

DECLARE_FAKE_VALUE_FUNC(int, opt3007_get_max_lux);
DECLARE_FAKE_VALUE_FUNC(int, is_in_box);
DECLARE_FAKE_VALUE_FUNC(uint32_t, chargerGetVdcRect);
extern uint32_t imu_pitch_x100;
extern atomic_t dashboard_head_up_opened;

DECLARE_FAKE_VOID_FUNC(set_brightness_to_panel_reg_in_running, uint8_t);
extern uint32_t JDB_PANEL_ID;

DECLARE_FAKE_VALUE_FUNC(const char *, get_string, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, day_of_week, int, int, int);

/* no entropy driver in the unit build */
DECLARE_FAKE_VALUE_FUNC(uint32_t, z_impl_sys_rand32_get);

DECLARE_FAKE_VALUE_FUNC(int, g1_fake_panel_set_brightness, struct jbd_panel_context *, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_spi_init, struct spi_master_ctx *);
/* not installed by default */
DECLARE_FAKE_VOID_FUNC(g1_fake_switch_to_dfu, struct k_sem *);
DECLARE_FAKE_VOID_FUNC(g1_fake_flash_ops_fn, struct flash_ops *);

#endif /* G1_UNIT_FAKES_MISC_H_ */
