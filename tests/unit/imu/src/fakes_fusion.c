#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "fakes_fusion.h"
#include "harness.h"

DEFINE_FAKE_VOID_FUNC(report_event_to_phone, int);
DEFINE_FAKE_VALUE_FUNC(int, send_response_data_to_ble);
DEFINE_FAKE_VOID_FUNC(exit_temp_task, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(uint8_t, is_persist_task_active, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, get_work_mode);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_dashboard_startup_mode);
DEFINE_FAKE_VALUE_FUNC(uint8_t, cur_message_show_state);

__weak atomic_t dashboard_lock_requested;
__weak atomic_t dashboard_lock_reported;
__weak uint8_t push_msg_syncing;
__weak uint8_t screen_switch_busy;

static void fakes_fusion_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(report_event_to_phone);
	RESET_FAKE(send_response_data_to_ble);
	RESET_FAKE(exit_temp_task);
	RESET_FAKE(is_persist_task_active);
	RESET_FAKE(get_work_mode);
	RESET_FAKE(get_dashboard_startup_mode);
	RESET_FAKE(cur_message_show_state);
	atomic_clear(&dashboard_lock_requested);
	atomic_clear(&dashboard_lock_reported);
	push_msg_syncing = 0;
	screen_switch_busy = 0;
}

G1_UNIT_RESET_HOOK(fakes_fusion_reset);
