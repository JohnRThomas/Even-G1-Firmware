#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "fakes_display.h"
#include "harness.h"

#include "dashboard/dashboard.h"
#include "display/display_thread.h"
#include "gui/gui.h"
#include "notify/notify.h"
#include "threads/threads.h"
#include "ui/ui.h"

DEFINE_FAKE_VALUE_FUNC(int, display_DelayClose, int);

DEFINE_FAKE_VOID_FUNC(exit_running_task, struct GlassesState *, int);
DEFINE_FAKE_VOID_FUNC(exit_running_task_keep_status, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, update_persist_task_status, struct GlassesState *, int, int);
DEFINE_FAKE_VALUE_FUNC(bool, is_idle);
DEFINE_FAKE_VOID_FUNC(set_msg_timeout_flag);
__weak uint8_t msg_busy;

DEFINE_FAKE_VOID_FUNC(cleanCalenadrIndex);
DEFINE_FAKE_VOID_FUNC(cleanNewsIndex);
DEFINE_FAKE_VOID_FUNC(cleanNotesIndex);
DEFINE_FAKE_VOID_FUNC(cleanStocksIndex);
DEFINE_FAKE_VALUE_FUNC(uint8_t, getNotesIndex);
DEFINE_FAKE_VALUE_FUNC(uint32_t, get_dashboard_tx_ctx);
DEFINE_FAKE_VALUE_FUNC(uint32_t, get_date);
DEFINE_FAKE_VALUE_FUNC(uint32_t, set_date, uint32_t);

DEFINE_FAKE_VOID_FUNC(countdown_reset, struct countdown_ts_context *);
DEFINE_FAKE_VOID_FUNC(even_ai_v2_info_timeout_reset);
DEFINE_FAKE_VOID_FUNC(transcribe_info_timeout_reset);
DEFINE_FAKE_VOID_FUNC(quicknote_timeout_reset);
DEFINE_FAKE_VOID_FUNC(request_not_disturb_bitmap, uint8_t);
DEFINE_FAKE_VOID_FUNC(show_not_disturb_bitmap, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, onboarding_ble_process, struct GlassesState *,
		       const struct ble_req_hdr *, const uint8_t *, uint8_t *);

static uint32_t set_date_default(uint32_t ts)
{
	get_dashboard_state()->date_ts = ts;
	return ts;
}

static uint32_t get_date_default(void)
{
	return get_dashboard_state()->date_ts;
}

static uint32_t get_dashboard_tx_ctx_default(void)
{
	return get_dashboard_state()->dashboard_ts->ts;
}

static void fakes_display_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	RESET_FAKE(display_DelayClose);
	RESET_FAKE(exit_running_task);
	RESET_FAKE(exit_running_task_keep_status);
	RESET_FAKE(update_persist_task_status);
	RESET_FAKE(is_idle);
	RESET_FAKE(set_msg_timeout_flag);
	msg_busy = 0;

	RESET_FAKE(cleanCalenadrIndex);
	RESET_FAKE(cleanNewsIndex);
	RESET_FAKE(cleanNotesIndex);
	RESET_FAKE(cleanStocksIndex);
	RESET_FAKE(getNotesIndex);
	RESET_FAKE(get_dashboard_tx_ctx);
	RESET_FAKE(get_date);
	RESET_FAKE(set_date);
	set_date_fake.custom_fake = set_date_default;
	get_date_fake.custom_fake = get_date_default;
	get_dashboard_tx_ctx_fake.custom_fake = get_dashboard_tx_ctx_default;

	RESET_FAKE(countdown_reset);
	RESET_FAKE(even_ai_v2_info_timeout_reset);
	RESET_FAKE(transcribe_info_timeout_reset);
	RESET_FAKE(quicknote_timeout_reset);
	RESET_FAKE(request_not_disturb_bitmap);
	RESET_FAKE(show_not_disturb_bitmap);
	RESET_FAKE(onboarding_ble_process);
}

G1_UNIT_RESET_HOOK(fakes_display_reset);
