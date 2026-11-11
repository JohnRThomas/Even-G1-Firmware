/* Display, task, dashboard and UI fakes; set_date/get_date go through GlassesState->date_ts. */
#ifndef G1_UNIT_FAKES_DISPLAY_H_
#define G1_UNIT_FAKES_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

struct ble_req_hdr;

DECLARE_FAKE_VALUE_FUNC(int, display_DelayClose, int);

DECLARE_FAKE_VOID_FUNC(exit_running_task, struct GlassesState *, int);
DECLARE_FAKE_VOID_FUNC(exit_running_task_keep_status, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, update_persist_task_status, struct GlassesState *, int, int);
DECLARE_FAKE_VALUE_FUNC(bool, is_idle);
DECLARE_FAKE_VOID_FUNC(set_msg_timeout_flag);
extern uint8_t msg_busy;

DECLARE_FAKE_VOID_FUNC(cleanCalenadrIndex);
DECLARE_FAKE_VOID_FUNC(cleanNewsIndex);
DECLARE_FAKE_VOID_FUNC(cleanNotesIndex);
DECLARE_FAKE_VOID_FUNC(cleanStocksIndex);
DECLARE_FAKE_VALUE_FUNC(uint8_t, getNotesIndex);
DECLARE_FAKE_VALUE_FUNC(uint32_t, get_dashboard_tx_ctx);
DECLARE_FAKE_VALUE_FUNC(uint32_t, get_date);
DECLARE_FAKE_VALUE_FUNC(uint32_t, set_date, uint32_t);

DECLARE_FAKE_VOID_FUNC(countdown_reset, struct countdown_ts_context *);
DECLARE_FAKE_VOID_FUNC(even_ai_v2_info_timeout_reset);
DECLARE_FAKE_VOID_FUNC(transcribe_info_timeout_reset);
DECLARE_FAKE_VOID_FUNC(quicknote_timeout_reset);
DECLARE_FAKE_VOID_FUNC(request_not_disturb_bitmap, uint8_t);
DECLARE_FAKE_VOID_FUNC(show_not_disturb_bitmap, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, onboarding_ble_process, struct GlassesState *,
			const struct ble_req_hdr *, const uint8_t *, uint8_t *);

#endif /* G1_UNIT_FAKES_DISPLAY_H_ */
