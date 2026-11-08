/*
 * Leaving the running task: stop the microphone, clear every feature's "active"
 * state and return the task status to idle.
 */
#include <string.h>
#include "services/tasks/tasks.h"
#include "comm/ble/report.h"
#include "ui/screen_task.h"
#include "drivers/mic/dmic.h"
void exit_running_task(struct GlassesState *state, int notify)
{
	uint8_t changed = 0;

	if (!is_task_running(state)) {
		return;
	}
	if (get_dashboard_state()->stay_time_left_s) {
		get_dashboard_state()->stay_time_left_s = 0;
	}
	if (state->translate_src->data[0] || state->translate_dst->data[0]) {
		dmic_stream_stop();
		memset(state->translate_dst->data, 0, 0x1e0);
		memset(state->translate_src->data, 0, 0x1e0);
		changed = true;
	}
	if (state->nav->running) {
		state->nav->running = 0;
		changed = true;
	}
	if (state->teleprompter->ready) {
		dmic_stream_stop();
		state->teleprompter->ready = 0;
		changed = true;
	}
	if (state->quicknote_show_info->enable) {
		dmic_stream_stop();
		state->quicknote_show_info->enable = 0;
		changed = true;
	}
	if (state->dashboard_mode == 6) {
		dmic_stream_stop();
		state->dashboard_ts->dashboard_on = 0;
		changed = true;
	}
	if (state->dashboard_ts->exit_pending == 1) {
		state->dashboard_ts->exit_pending = 2;
	}
	if (state->dashboard_mode == 4) {
		changed = true;
	}
	if (state->onboarding->active) {
		state->onboarding->active = 0;
		changed = true;
	}
	if (state->raster_height_cfg[0]) {
		state->raster_height_cfg[0] = 0;
		changed = true;
	}
	if (state->dash_buf->data[0]) {
		dmic_stream_stop();
		reset_dash_buf(1);
		changed = true;
	}
	if (state->evenai_buf->state == 1) {
		dmic_stream_stop();
		state->evenai_buf->state = 0;
		changed = true;
	} else if (state->evenai_buf->state == 2) {
		state->evenai_buf->state = 3;
	}
	if (state->horiz_pitch_cfg[0]) {
		state->horiz_pitch_cfg[0] = 0;
		changed = true;
	}
	if (state->ts_info->enable) {
		state->ts_info->enable = 0;
		changed = true;
	}
	if (state->transcribe_buf->running) {
		dmic_stream_stop();
		state->transcribe_buf->running = 0;
		changed = true;
	}
	if (!changed) {
		return;
	}
	if (notify) {
		report_event_to_phone(0);
	}
	update_persist_task_status_to_idle(state);
	if (get_dashboard_state()->sync_seq == 6) {
		return;
	}
	memset(state->ui.buf, 0, sizeof(state->ui.buf));
}

void exit_running_task_keep_status(struct GlassesState *state, int notify)
{
	uint8_t changed = 0;

	if (!is_task_running(state)) {
		return;
	}
	if (get_dashboard_state()->stay_time_left_s) {
		get_dashboard_state()->stay_time_left_s = 0;
	}
	if (state->translate_src->data[0] || state->translate_dst->data[0]) {
		dmic_stream_stop();
		memset(state->translate_dst->data, 0, 0x1e0);
		memset(state->translate_src->data, 0, 0x1e0);
		changed = true;
	}
	if (state->nav->running) {
		changed = true;
		state->nav->running = 0;
	}
	if (state->teleprompter->ready) {
		dmic_stream_stop();
		changed = true;
		state->teleprompter->ready = 0;
	}
	if (state->quicknote_show_info->enable) {
		dmic_stream_stop();
		changed = true;
		state->quicknote_show_info->enable = 0;
	}
	if (state->dashboard_mode == 6) {
		dmic_stream_stop();
		state->dashboard_ts->dashboard_on = 0;
	}
	if (state->dashboard_ts->exit_pending == 1) {
		state->dashboard_ts->exit_pending = 2;
	}
	if (state->bitmap_info->enable) {
		changed = true;
		state->bitmap_info->enable = 0;
	}
	if (state->onboarding->active) {
		changed = true;
		state->onboarding->active = 0;
	}
	if (state->dash_buf->data[0]) {
		dmic_stream_stop();
		reset_dash_buf(1);
		changed = true;
	}
	if (state->evenai_buf->state == 1) {
		dmic_stream_stop();
		state->evenai_buf->state = 0;
		changed = true;
	} else if (state->evenai_buf->state == 2) {
		state->evenai_buf->state = 3;
	}
	if (state->raster_height_cfg[0]) {
		changed = true;
		state->raster_height_cfg[0] = 0;
	}
	if (state->horiz_pitch_cfg[0]) {
		changed = true;
		state->horiz_pitch_cfg[0] = 0;
	}
	if (state->ts_info->enable) {
		state->ts_info->enable = 0;
		changed = true;
	}
	if (state->transcribe_buf->running) {
		dmic_stream_stop();
		state->transcribe_buf->running = 0;
		changed = true;
	}
	if (!changed) {
		return;
	}
	if (notify) {
		report_event_to_phone(0);
	}
	memset(state->ui.buf, 0, sizeof(state->ui.buf));
}

void exit_temp_task(struct GlassesState *state, int flag)
{
	if (!is_task_running(state)) {
		return;
	}
	if (state->dashboard_mode == 6) {
		dmic_stream_stop();
		state->dashboard_ts->dashboard_on = 0;
	} else if (state->dashboard_mode != 4) {
		return;
	}
	if (flag) {
		report_event_to_phone(0);
	}
	update_persist_task_status_to_idle(state);
	memset(state->ui.buf, 0, sizeof(state->ui.buf));
}
