/*
 * Screen refresh thread: once per round it collects the running screen's data
 * block, syncs it to the slave lens (on the master) and redraws the screen when
 * the data changed.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include "ui/screen_dispatch.h"
#include "system/logging.h"
#include "system/work_mode.h"
#include "main.h"
#include "comm/ble/report.h"
#include "comm/esb/esb.h"
#include "drivers/display/panel.h"
#include "services/notifications/notify.h"
#include "services/production_test/production_test.h"
#include "services/tasks/tasks.h"
#include "ui/display_thread.h"
#include "ui/screen_task.h"
#include "ui/screens/ui.h"
#include "drivers/ambient/opt3007.h"
#include "ui/screens/ui_raster_height.h"
#include "ui/screens/ui_countdown.h"
#include "drivers/display/jbd_panel.h"
#include "services/production_test/pt_touch.h"

uint8_t deferred_text_ack_cmd; /* 0x0d / 0x0f / 0x4e whose last-packet ack waits for the display
				  refresh */
uint8_t deferred_text_ack_total;
uint8_t deferred_text_ack_idx;
uint8_t screen_switch_busy;
struct k_mutex screen_payload_mutex;
static uint8_t last_msg_screen;
static uint32_t date_ts;
static int date_idx;
static int date_cnt;

static K_THREAD_STACK_DEFINE(display_dispatch_stack, 0x800);
static struct k_thread display_dispatch_thread_data;
static void unlock_screen_payload_mutex(void)
{
	k_mutex_unlock(&screen_payload_mutex);
}

static void display_dispatch_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	int last_attitude = 1;
	int bright_cnt = 0;
	uint8_t changed = 0;
	uint8_t *data = NULL;
	int wait;
	size_t len = 0;
	uint8_t prev_changed;
	uint8_t screen;
	bool refresh;

	G1_LOG_WRN("enter\n");
	state->panel.f_360 = 0x12;
	state->screen_on = 0;
	G1_LOG_ERR("raster_height_gear:%d, canvas_distance_gear:%d\n",
		   state->panel.raster_height_offset, state->panel.canvas_distance_gear);
	state->panel.current_row = 0;
	cal_panel_canvas_coord(&get_dashboard_state()->panel.y_offset,
			       (int *)&get_dashboard_state()->panel.current_column);
	state->sync_canvas_x = state->panel.current_row + state->canvas_x;
	state->sync_canvas_y = state->panel.current_column + state->canvas_y;
	state->imu.on = 0;
	state->panel_on = 1;
	state->dashboard_mode = 0;
	if (state->display_mode == 11) {
		change_work_mode(2);
		if (get_pt_test_mode() == 1) {
			enter_silent_mode();
		}
	}

	for (;;) {
		while (state->work_mode == 1 || get_dashboard_state()->work_mode == 8) {
			k_sleep(K_MSEC(5000));
		}
		if (!state->screen_on && !is_task_running(state)) {
			G1_LOG_WRN("master sync display suspend.\n");
			state->esb_arg2 = state->panel.panel_brightness_level;
			if (get_work_mode() != 3) {
				sync_to_slave(state, 1, NULL, 0);
				change_work_mode(3);
			}
			if (state->is_master == 2) {
				while (k_sem_count_get(&state->sem_2)) {
					k_sem_take(&state->sem_2, K_FOREVER);
				}
				while (k_sem_count_get(&state->screen_on_sem)) {
					k_sem_take(&state->screen_on_sem, K_FOREVER);
				}
			}
			if (!state->screen_on && !is_task_running(state)) {
				G1_LOG_WRN("thread goto sleep.\n");
				screen_switch_busy = 0;
				k_sem_take(&state->screen_on_sem, K_FOREVER);
				if (state->is_master == 2) {
					while (k_sem_count_get(&state->sem_2)) {
						k_sem_take(&state->sem_2, K_FOREVER);
					}
				}
				bright_cnt = 4;
			} else {
				while (k_sem_count_get(&state->screen_on_sem)) {
					k_sem_take(&state->screen_on_sem, K_FOREVER);
				}
			}
			G1_LOG_WRN("thread wakeup!.\n");
			G1_LOG_WRN("sync display resume.\n");
			state->esb_arg2 = state->panel.panel_brightness_level;
			change_work_mode(2);
			changed = 1;
		}
		/* the time already spent waiting for the slave in this round */
		wait = 0;
		refresh = state->panel_on;
		if (refresh) {
			if (state->is_master == 1 && ++bright_cnt == 5) {
				if (state->opt3007_context.auto_brightness &&
				    state->opt3007_context.ready && state->panel.init_done) {
					uint8_t target = opt3007_get_brightness_level(
						&state->opt3007_context);

					if (target == 0xff) {
						bright_cnt = 0;
					} else {
						if (state->panel.panel_brightness_level < target) {
							state->panel.panel_brightness_level++;
						} else if (state->panel.panel_brightness_level >
							   target) {
							state->panel.panel_brightness_level--;
						}
						bright_cnt = (state->panel.panel_brightness_level !=
							      target)
								     ? 4
								     : 0;
					}
				} else {
					bright_cnt = 4;
				}
				if (state->panel.last_synced_brightness !=
				    state->panel.panel_brightness_level) {
					state->panel.last_synced_brightness =
						state->panel.panel_brightness_level;
					if ((state->dashboard_mode == 6 &&
					     state->dashboard_ts->area) ||
					    state->dashboard_mode == 10 ||
					    state->dashboard_mode == 4) {
						changed = 0;
					} else {
						changed = state->dashboard_mode != 20;
					}
				}
			}
			state->sync_status.task = state->task_status->task;
			state->sync_status.status = state->task_status->status;
			if (state->is_master == 1) {
				state->esb_up_seq = state->esb_up_id;
				state->esb_down_seq = state->esb_down_id;
				state->esb_dir = state->imu.mode;
				state->esb_screen_id = state->dashboard_mode;
			} else {
				int64_t t0;

				if (state->suspend_state == 1 || state->suspend_state == 2 ||
				    state->suspend_state == 4) {
					k_msleep(50);
					continue;
				}
				t0 = k_uptime_get();
				k_sem_take(&state->sem_2, K_MSEC(500));
				if (k_uptime_get() - t0 >= 500) {
					refresh = false;
				}
			}
		}
		if (refresh && state->display_mode == 12 && state->dashboard_mode != 7) {
			refresh = false;
		}
		if (refresh && !is_task_running(state)) {
			G1_LOG_ERR("no running task, goto next trun\n");
			refresh = false;
		}
		if (refresh) {
			k_mutex_lock(&screen_payload_mutex, K_FOREVER);
			screen = state->dashboard_mode;
			switch (state->dashboard_mode) {
			case 4: {
				uint8_t retry = 0x33;

				while (state->esb_screen_id != state->dashboard_mode && --retry) {
					k_msleep(10);
				}
				data = (uint8_t *)get_message_by_index(get_cur_show_msg_idx());
				len = sizeof(struct notif_msg);
				if (take_msg_timeout_flag() ||
				    get_dashboard_state()->unread_msg_num != last_msg_screen) {
					last_msg_screen = get_dashboard_state()->unread_msg_num;
					changed = 1;
				} else if (!changed) {
					refresh = false;
				}
				break;
			}
			case 2:
				countdown_tick(state->countdown_ts);
				len = sizeof(struct countdown_ts_context);
				data = (uint8_t *)state->countdown_ts;
				break;
			case 9:
				len = sizeof(struct teleprompter_info);
				data = (uint8_t *)state->teleprompter;
				break;
			case 12:
				len = sizeof(struct quicknote_show_info);
				data = (uint8_t *)state->quicknote_show_info;
				break;
			case 10: {
				uint8_t att = state->imu.mode;

				if (att == 2) {
					if (last_attitude != 2 ||
					    state->nav->imu_action_status != 2) {
						changed = 1;
						state->nav->imu_action_status = 2;
					}
				} else if (att != last_attitude ||
					   state->nav->imu_action_status != 1) {
					changed = 1;
					state->nav->imu_action_status = 1;
				}
				len = sizeof(struct nav_info);
				data = (uint8_t *)state->nav;
				break;
			}
			case 11: {
				struct translate_buf *dst = state->translate_dst;
				struct translate_buf *src = state->translate_src;
				uint8_t att;
				bool show_dst;

				if (dst->screen_status == 5 || dst->screen_status == 6) {
					if (state->is_master == 2 &&
					    state->esb_up_id != state->esb_up_seq &&
					    state->esb_up_snapshot[ESB_SNAPSHOT_SEQ] ==
						    state->esb_up_seq) {
						data = state->esb_up_snapshot;
					} else {
						data = (uint8_t *)dst;
					}
				} else if (src->screen_status == 5 || src->screen_status == 6) {
					if (state->is_master == 2 &&
					    state->esb_down_id != state->esb_down_seq &&
					    state->esb_down_snapshot[ESB_SNAPSHOT_SEQ] ==
						    state->esb_down_seq) {
						data = state->esb_down_snapshot;
					} else {
						data = (uint8_t *)src;
					}
				} else {
					att = state->imu.mode;
					if (dst->original_type == dst->translation_type ||
					    (att == 2 && last_attitude != 2)) {
						dst->screen_status = 2;
						show_dst = true;
					} else {
						show_dst = att == 2;
					}
					if (show_dst) {
						if (state->is_master == 2 &&
						    state->esb_up_id != state->esb_up_seq &&
						    state->esb_up_snapshot[ESB_SNAPSHOT_SEQ] ==
							    state->esb_up_seq) {
							data = state->esb_up_snapshot;
						} else {
							data = (uint8_t *)dst;
						}
					} else {
						if (att != last_attitude) {
							src->screen_status = 4;
						}
						if (state->is_master == 2 &&
						    state->esb_down_id != state->esb_down_seq &&
						    state->esb_down_snapshot[ESB_SNAPSHOT_SEQ] ==
							    state->esb_down_seq) {
							data = state->esb_down_snapshot;
						} else {
							data = (uint8_t *)src;
						}
					}
				}
				len = sizeof(struct translate_buf);
				break;
			}
			case 8:
				len = sizeof(struct user_schedule_context);
				data = (uint8_t *)state->user_sched_info;
				break;
			case 7:
				len = sizeof(struct bitmap_show_info);
				data = (uint8_t *)state->bitmap_info;
				break;
			case 14:
				get_dashboard_state()->panel.panel_brightness_level = 0x2a;
				len = sizeof(struct onboarding_ctx);
				data = (uint8_t *)state->onboarding;
				break;
			case 5:
			case 6:
			case 15:
				len = sizeof(struct dashboard_ts_context);
				data = (uint8_t *)state->dashboard_ts;
				break;
			case 16: {
				struct dash_data_buf *blk = state->dash_buf;

				if (blk->type == 13 || blk->type == 15) {
					refresh = false;
					break;
				}
				if (state->is_master == 2 &&
				    state->esb_up_id != state->esb_up_seq &&
				    state->esb_up_seq == state->esb_up_snapshot[ESB_SNAPSHOT_SEQ]) {
					data = state->esb_up_snapshot;
					blk->seq = state->esb_up_snapshot[ESB_SNAPSHOT_SEQ];
				} else {
					state->dash_buf->seq = get_dashboard_state()->esb_up_id;
					data = (uint8_t *)blk;
				}
				if (state->dash_buf->type == 7) {
					state->dash_buf->screen_status = 3;
				}
				len = sizeof(struct dash_data_buf);
				break;
			}
			case 20:
				len = sizeof(struct evenai_buf);
				data = (uint8_t *)state->evenai_buf;
				break;
			case 17:
				len = HORIZ_PITCH_CFG_SIZE;
				data = state->horiz_pitch_cfg;
				break;
			case 18:
				len = sizeof(struct prompt_info);
				data = (uint8_t *)state->ts_info;
				break;
			case 19:
				len = sizeof(struct transcribe_buf);
				data = (uint8_t *)state->transcribe_buf;
				break;
			case 3: {
				uint32_t now = k_uptime_get();

				if (!date_ts) {
					date_ts = now;
				} else if (now - date_ts > 9999) {
					date_idx = (date_idx + 1) % 7;
					date_ts = 0;
				}
				date_cnt++;
				state->sync_data[0] = date_idx;
				state->sync_data[1] = date_cnt;
				break;
			}
			default:
				break;
			}
			if (refresh && data) {
				if (memcmp(state->ui.buf, data, len) == 0 && !changed &&
				    state->dashboard_mode == last_ui_screen_id) {
					refresh = false;
				} else {
					memcpy(state->ui.buf, data, len);
				}
			}
			if (refresh) {
				memset(&state->ui.buf[len], 0, sizeof(state->ui.buf) - len);
				state->buf_len.v = len;
				prev_changed = changed;
			}
			unlock_screen_payload_mutex();
		}
		if (refresh && state->suspend_state != 0 && state->suspend_state != 5) {
			refresh = false;
		}
		if (refresh && state->is_master == 1) {
			if (state->esb_arg2 != state->panel.panel_brightness_level) {
				set_brightness_to_panel_reg_in_running(
					state->panel.panel_brightness_level);
				state->esb_arg2 = state->panel.panel_brightness_level;
			}
			wait = sync_to_slave(state, 0, NULL, 0);
			changed = 1;
			if (wait >= 5000) {
				G1_LOG_ERR("sync to slave exceed MAX_WAIT_COUNT, wait_time %d\n",
					   wait);
				wait = 0;
				refresh = false;
			} else if (wait >= 3000) {
				G1_LOG_ERR("sync to slave exceed CMD_WAIT_COUNT, wait_time %d\n",
					   wait);
				refresh = false;
			} else if (wait >= 500) {
				G1_LOG_ERR("sync to slave exceed LOW_WAIT_COUNT, wait_time %d\n",
					   wait);
				refresh = false;
			} else if (state->esb_screen_id != state->dashboard_mode &&
				   state->dashboard_mode > 6 && state->esb_screen_id > 6) {
				G1_LOG_ERR(
					"screen id was changed, g->master_sync_pkg.new_screen_id %d pkg->screen_id %d\n",
					state->esb_screen_id, state->dashboard_mode);
				wait = 70;
				refresh = false;
			}
		}
		if (refresh) {
			if (!is_task_running(state)) {
				G1_LOG_ERR("task was closed!!!\n");
				changed = 1;
			} else if (state->dashboard_mode != screen) {
				G1_LOG_ERR("screen is closed\n");
				changed = 1;
			} else if (!state->screen_on) {
				G1_LOG_ERR("trigger_on_screen is closed\n");
				changed = 1;
			} else {
				uint8_t att = state->imu.mode;

				if (state->dashboard_mode == 10 && last_attitude != att &&
				    prev_changed != 1) {
					G1_LOG_WRN("imu->attitude changed ....\n");
				} else {
					last_attitude = att;
				}
				display_reflash_screen(state->dashboard_mode);
				G1_LOG_ERR("display_reflash_screen %d done\n",
					   state->dashboard_mode);
				changed = 0;
			}
		}
		if (state->is_master == 1) {
			switch (deferred_text_ack_cmd) {
			case 0x0d:
				report_event_to_phone(0xf0);
				break;
			case 0x0f:
				report_event_to_phone(0xf1);
				break;
			case 0x4e:
				report_event_to_phone(0xf2);
				break;
			default:
				break;
			}
			deferred_text_ack_cmd = 0;
		}
		if (state->is_master == 1) {
			if (wait < 70) {
				k_sleep(K_MSEC(70 - wait));
			}
		} else if (state->suspend_state != 0 && state->suspend_state != 5) {
			k_sleep(K_MSEC(10));
		}
	}
}

void screen_dispatch_init(void)
{
	k_mutex_init(&screen_payload_mutex);
}

void screen_dispatch_start(struct GlassesState *state)
{
	k_thread_create(&display_dispatch_thread_data, display_dispatch_stack,
			K_THREAD_STACK_SIZEOF(display_dispatch_stack), display_dispatch_thread,
			state, NULL, NULL, -13, 0, K_NO_WAIT);
}
