/*
 * Slave side of the lens-to-lens sync: the thread that applies the master's sync
 * packets (screen changes, suspend/resume, settings, key events, notifications)
 * to the slave's own state and display.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "comm/esb/esb.h"
#include "comm/ble/ble.h"
#include "system/logging.h"
#include "system/system_state.h"
#include "system/work_mode.h"
#include "drivers/display/panel.h"
#include "services/notifications/notify.h"
#include "services/production_test/production_test.h"
#include "services/storage/storage.h"
#include "services/tasks/tasks.h"
#include "ui/display_thread.h"
#include "ui/gui/gui.h"
#include "ui/screen_dispatch.h"
#include "ui/screen_task.h"
#include "ui/screens/dashboard.h"
#include "ui/screens/ui.h"
#include "util/date.h"
#include "system/clock.h"
#include "ui/screens/ui_bitmap.h"
#include "ui/screens/ui_onboarding.h"
#include "ui/screens/ui_raster_height.h"
#include "drivers/display/jbd_panel.h"
#include "services/storage/setting_store.h"

uint8_t display_suspend_received;

static K_THREAD_STACK_DEFINE(slave_display_stack, 0x600);
static struct k_thread slave_display_thread_data;
static void slave_display_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;

	state->panel_on = 1;
	state->dashboard_mode = 0;
	G1_LOG_WRN("enter\n");
	if (state->display_mode == 11) {
		change_work_mode(2);
	}
	for (;;) {
		if (state->display_mode != 11) {
			k_sem_take(&state->sem_1, K_FOREVER);
		} else {
			state->sync_hdr.cmd = 0;
			if (!get_pt_test_mode()) {
				state->dashboard_mode = 6;
			} else {
				state->dashboard_mode = 3;
			}
			k_sleep(K_MSEC(1000));
		}
		while (state->work_mode == 1 || get_dashboard_state()->work_mode == 8) {
			k_sleep(K_MSEC(5000));
		}
		if (state->esb_arg2 != state->panel.applied_brightness ||
		    state->panel.panel_brightness_level != state->esb_arg2) {
			if (get_pt_test_mode() == 1) {
				set_brightness_to_panel_reg_in_running(
					state->panel.panel_brightness_level);
				state->esb_arg2 = state->panel.panel_brightness_level;
				state->panel.applied_brightness =
					state->panel.panel_brightness_level;
			} else {
				state->panel.applied_brightness = state->esb_arg2;
				state->panel.panel_brightness_level = state->esb_arg2;
				set_brightness_to_panel_reg_in_running(state->esb_arg2);
			}
		}

		switch (state->sync_hdr.cmd) {
		case 0:
			if (state->suspend_state == 1) {
				if (state->dashboard_mode != 6 && !is_task_running(state)) {
					G1_LOG_ERR("Ignore data...\n");
					break;
				}
				change_work_mode(2);
				if (state->dashboard_mode == 6 && state->imu.mode != 2) {
					state->imu.mode = 2;
				}
			}
			k_sem_give(&state->sem_2);
			break;
		case 1:
			display_suspend_received = 1;
			screen_switch_busy = 1;
			if (state->suspend_state == 0) {
				exit_running_task(state, 0);
				state->imu.mode = 1;
				state->task_status->task = 0;
				trigger_screen_state_change("goto idle2", state, 0);
				k_sem_give(&state->sem_2);
			} else {
				k_sleep(K_MSEC(80));
			}
			break;
		case 2:
			if (state->sync_data[0] == 11) {
				G1_LOG_WRN(
					"slave rcv canvas_distance_gear:%d,raster_height_gear:%d\n",
					state->sync_data[1], state->sync_data[2]);
				if (state->sync_data[1] < 10 && state->sync_data[2] < 9) {
					get_dashboard_state()->panel.canvas_distance_gear =
						state->sync_data[1];
					get_dashboard_state()->panel.raster_height_offset =
						state->sync_data[2];
					cal_panel_canvas_coord(
						&get_dashboard_state()->panel.y_offset,
						(int *)&get_dashboard_state()
							->panel.current_column);
				}
			}
			screen_switch_busy = 1;
			if (state->dashboard_mode == 9 || state->dashboard_mode == 12 ||
			    state->dashboard_mode == 10 || state->dashboard_mode == 11 ||
			    state->dashboard_mode == 6 || state->dashboard_mode == 7 ||
			    state->dashboard_mode == 14 || state->dashboard_mode == 15 ||
			    state->dashboard_mode == 16 || state->dashboard_mode == 20 ||
			    state->dashboard_mode == 17 || state->dashboard_mode == 18 ||
			    state->dashboard_mode == 19 || state->dashboard_mode == 0) {
				state->imu.mode = 2;
			}
			change_work_mode(2);
			k_sem_give(&state->sem_2);
			screen_switch_busy = 0;
			break;
		case 3:
			exit_running_task(state, 1);
			change_work_mode(7);
			break;
		case 6:
			if (state->sync_data[0] == 9) {
				upgradeDashboardStartupModeInfoToFlash(state->sync_data[1]);
				break;
			}
			switch (state->sync_data[0]) {
			case 10:
				atomic_or(&dashboard_flags, 2);
				if (state->sync_data[1] == 1) {
					if (get_dashboard_state()->double_tap_customize == 4) {
						state->double_tap_dashboard_on = 1;
					}
				} else if (state->sync_data[1] == 2) {
					state->double_tap_dashboard_on = 0;
					state->dashboard_ts->dashboard_hold = 0;
				}
				break;
			case 1:
				if (state->dashboard_mode == 12) {
					if (state->sync_data[1] == 0) {
						display_DelayClose(10000);
						k_yield();
					}
				} else if (state->sync_data[1] == 1) {
					exit_temp_task(state, 0);
					state->esb_screen_id = 12;
					state->quicknote_show_info->enable = 1;
					state->quicknote_show_info->started_by_app = 0;
					state->quicknote_show_info->action = 7;
					memset(get_dashboard_state()->quicknote_show_info->data, 0,
					       0x14a);
					update_persist_task_status(state, 12, 2);
					k_sem_give(&state->sem_2);
				}
				break;
			case 2: {
				uint8_t v = state->sync_data[1];
				uint8_t key = v & 0xf;
				uint8_t idx;

				if (key == 1 && state->dashboard_mode == 6) {
					setNotesIndex(v >> 4);
					display_inputEvent(0, 1);
					k_yield();
				} else if (key == 2) {
					setNotesIndex(v >> 4);
				} else if (key == 3 && state->dashboard_mode == 6) {
					setStocksIndex(v >> 4);
					display_inputEvent(0, 1);
					k_yield();
				} else if (key == 4) {
					setStocksIndex(v >> 4);
				} else if (key == 5 && state->dashboard_mode == 6) {
					setNewsIndex(v >> 4);
					display_inputEvent(0, 1);
					k_yield();
				} else if (key == 6) {
					setNewsIndex(v >> 4);
				} else if (key == 7 && state->dashboard_mode == 6) {
					idx = v >> 4;
					setCalenadrIndex(idx);
					display_inputEvent(0, 1);
					k_yield();
					G1_LOG_ERR(
						"received master send touch si gle click command,timestamp = %d,exec key function %d\n",
						get_date(), idx);
				} else if (key == 8) {
					idx = v >> 4;
					setCalenadrIndex(idx);
					G1_LOG_ERR(
						"received master send calendar index sync commmand ,timestamp = %d,exec index update %d\n",
						get_date(), idx);
				} else {
					if (v == 9) {
						state->dashboard_ts->exit_pending = 2;
					} else {
						G1_LOG_WRN(
							"received key press event ,slave screen id = %d,drop it ...\n",
							state->dashboard_mode);
					}
				}
				break;
			}
			case 3:
				if (state->dashboard_mode == 9) {
					if (state->sync_data[1] == 1) {
						display_MasterSendClose();
						k_yield();
					} else {
						G1_LOG_ERR(
							"slave received unknown command,drop it\n");
					}
				} else {
					G1_LOG_ERR(
						"The master Send the teleprompter command, but the slave's current ScreenID is not teleprompter.drop this command..\n");
				}
				break;
			case 6:
				if (state->dashboard_mode == 10) {
					if (state->sync_data[1] == 1) {
						display_MasterSendClose();
						k_yield();
					} else {
						G1_LOG_ERR(
							"slave received unknown command,drop it\n");
					}
				} else {
					G1_LOG_ERR(
						"The master Send the navigation command, but the slave's current ScreenID is not navigation.drop this command..\n");
				}
				break;
			case 7:
				if (state->dashboard_mode == 6) {
					display_powerEvent(state->sync_data[1]);
					k_yield();
				} else {
					G1_LOG_ERR(
						"The master Send the power notify command, but the slave's current ScreenID is not dashboard.drop this command..\n");
				}
				break;
			case 8: {
				uint8_t lang = state->sync_data[1];

				if (CURRENT_LANGUAGE.magic != lang) {
					G1_LOG_WRN(
						"The information sent by the host is different from that of the APP\n");
				}
				upgradeAppLanguageInfoToFlash(lang);
				break;
			}
			case 4: {
				uint8_t show;

				state->esb_down_id = state->onboarding->sub_step;
				state->onboarding->action = state->sync_data[1];
				state->onboarding->sub_step = state->sync_data[2];
				onboarding_check_step_finished();
				onboarding_shown_sub_step = state->sync_data[2];
				show = state->sync_data[3];
				gui_dynamic_bitmap_reset_thunk();
				onboarding_request_redraw();
				state->onboarding->ts = k_uptime_get();
				if (state->task_status->task != 14 &&
				    (!is_ble_device_bound() || state->onboarding->in_progress)) {
					if (!is_ble_device_bound()) {
						G1_LOG_WRN(
							"ble device unbound,prepare to show binding tip\n");
					}
					if (show == 1) {
						state->onboarding->active = 1;
						update_persist_task_status(state, 14, 2);
						state->onboarding->saved_brightness =
							state->panel.panel_brightness_level;
						state->panel.panel_brightness_level = 0x2a;
					}
				} else if (state->onboarding->action == 3) {
					if (onboarding_shown_sub_step == 0x12) {
						state->onboarding->sync_arg = show;
						state->onboarding->calib_state_a = 3;
						state->onboarding->calib_state_b = 3;
					}
				} else if (state->onboarding->action == 11) {
					get_dashboard_state()->onboarding->active = 0;
					get_dashboard_state()->onboarding->in_progress = 0;
				}
				trigger_screen_state_change("IMU:wakeup:onboarding", state, 1);
				k_sem_give(&state->sem_2);
				break;
			}
			case 5:
				show_not_disturb_bitmap(state->sync_data[1]);
				trigger_screen_state_change("TOUCH:wakeup:not_disturb", state, 1);
				k_sem_give(&state->sem_2);
				break;
			case 12:
				upgradeDoubleTapCustomizeToFlash(state->sync_data[1]);
				break;
			case 13:
				upgradeLongpressTapCustomizeToFlash(state->sync_data[1]);
				break;
			case 14:
				if (state->sync_data[1] == 6) {
					exit_temp_task(get_dashboard_state(), 0);
					if (get_dashboard_state()->task_status->task == 0x14 &&
					    get_dashboard_state()->evenai_buf->state == 1 &&
					    get_dashboard_state()->evenai_buf->sub == 4 &&
					    get_dashboard_state()->evenai_buf->action_status ==
						    0x13) {
						G1_LOG_WRN(
							"evenai_v2 need to show network_err tip\n");
						get_dashboard_state()->task_status->status = 2;
						break;
					}
					if (get_dashboard_state()->task_status->task != 0x14) {
						get_dashboard_state()->evenai_buf->state = 1;
						memset(get_dashboard_state()->evenai_buf->data, 0,
						       0x200);
						update_persist_task_status(get_dashboard_state(),
									   0x14, 2);
					}
					get_dashboard_state()->evenai_buf->sub = 7;
					get_dashboard_state()->evenai_buf->action_status =
						state->sync_data[2];
				}
				k_sem_give(&state->sem_2);
				break;
			default:
				break;
			}
			break;
		case 4:
			exit_running_task(state, 1);
			state->display_mode = 12;
			state->dashboard_cmd = 3;
			k_sem_give(&state->dashboard_position_sem);
			break;
		case 5:
			state->display_mode = 10;
			state->dashboard_cmd = 3;
			k_sem_give(&state->dashboard_position_sem);
			show_not_disturb_bitmap(1);
			break;
		case 7:
			switch (state->sync_data[0]) {
			case 0:
				update_temp_task_status(state, 5, 2);
				trigger_screen_state_change("BLE:wakeup:new-unread_msg", state, 1);
				break;
			case 1:
				msg_busy = 1;
				clear_timeout_message(0);
				set_msg_timeout_flag();
				mark_cur_message_read();
				update_temp_task_status(state, 4, 2);
				trigger_screen_state_change("IMU:wakeup:uncomplete msg", state, 1);
				break;
			case 2:
				g_arrMsgContent_count();
				if (get_dashboard_state()->unread_msg_num) {
					set_msg_show_state(6);
				} else {
					set_msg_show_state(0);
				}
				break;
			case 3:
				new_msg_signal_received = 1;
				if (get_dashboard_state()->message_mode &&
				    count_unread_messages() &&
				    get_dashboard_state()->dashboard_mode != 4) {
					update_temp_task_status(get_dashboard_state(), 4, 2);
					trigger_screen_state_change("IMU:wakeup:new-notification",
								    get_dashboard_state(), 1);
					set_msg_timeout_flag();
				}
				handle_msg_touch_key();
				k_sem_give(&get_dashboard_state()->sem_2);
				set_cur_message_state();
				break;
			default:
				break;
			}
			k_sem_give(&state->sem_2);
			break;
		default:
			break;
		}
	}
}

void esb_slave_start(struct GlassesState *state)
{
	k_thread_create(&slave_display_thread_data, slave_display_stack,
			K_THREAD_STACK_SIZEOF(slave_display_stack), slave_display_thread, state,
			NULL, NULL, -11, 0, K_NO_WAIT);
}
