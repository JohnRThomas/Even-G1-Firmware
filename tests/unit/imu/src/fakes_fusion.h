/* Weak FFF fakes of what imu_fusion.c calls outside the common fakes. */
#ifndef G1_UNIT_FAKES_FUSION_H_
#define G1_UNIT_FAKES_FUSION_H_

#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/sys/atomic.h>
#include <glasses_state.h>

DECLARE_FAKE_VOID_FUNC(report_event_to_phone, int);
DECLARE_FAKE_VALUE_FUNC(int, send_response_data_to_ble);
DECLARE_FAKE_VOID_FUNC(exit_temp_task, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(uint8_t, is_persist_task_active, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, get_work_mode);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_dashboard_startup_mode);
DECLARE_FAKE_VALUE_FUNC(uint8_t, cur_message_show_state);

extern atomic_t dashboard_lock_requested;
extern atomic_t dashboard_lock_reported;
extern uint8_t push_msg_syncing;
extern uint8_t screen_switch_busy;

#endif /* G1_UNIT_FAKES_FUSION_H_ */
