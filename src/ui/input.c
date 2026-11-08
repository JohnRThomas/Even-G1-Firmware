/*
 * Touch input: the touch controller's press/release events are turned into
 * single, double and triple clicks and long presses, and those into actions on
 * the running screen (key_event_thread).
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "ui/input.h"
#include "system/logging.h"
#include "system/monitor.h"
#include "system/system_state.h"
#include "main.h"
#include "comm/ble/report.h"
#include "comm/esb/esb.h"
#include "drivers/gpio/board_gpio.h"
#include "drivers/touch/aw9320x.h"
#include "services/notifications/notify.h"
#include "services/production_test/production_test.h"
#include "services/storage/storage.h"
#include "services/tasks/tasks.h"
#include "ui/display_thread.h"
#include "ui/screen_task.h"
#include "ui/screens/dashboard.h"
#include "ui/screens/ui.h"
#include "ui/screens/ui_even_ai.h"
#include "ui/screens/ui_bitmap.h"
#include "ui/screens/ui_onboarding.h"
#include "services/storage/settings_records.h"
#include "services/production_test/pt_touch.h"

uint8_t long_press_active;
static uint8_t key_event_sem_ready;
static uint8_t lp_timeout_handled;
static struct k_sem key_event_sem;
static int key_event_type;
static int sw0_low_count;

static K_THREAD_STACK_DEFINE(touch_key_stack, 0x400);
static K_THREAD_STACK_DEFINE(key_event_stack, 0x400);
static struct k_thread touch_key_thread_data;
static struct k_thread key_event_thread_data;

static void init_key_event_sem(void);
static void trigger_key_event(void);
static void check_disp_quicknote(void)
{
	if (get_dashboard_state()->is_master == 1) {
		uint8_t data[2];
		int i;

		if (get_dashboard_state()->quicknote_show_info->longpress_req == 1) {
			get_dashboard_state()->quicknote_show_info->longpress_req = 0;
			if (get_dashboard_state()->dashboard_mode == 12) {
				G1_LOG_ERR("quicknote is running, exit this time\n");
				return;
			}
			data[0] = 1;
			data[1] = 1;
			if (sync_to_slave(get_dashboard_state(), 6, data, 2) >= 5000) {
				G1_LOG_WRN(
					"SYNC TO Slave failed...,don't exec quick note start function.\n");
			} else {
				exit_temp_task(get_dashboard_state(), 0);
				get_dashboard_state()->esb_screen_id = 12;
				get_dashboard_state()->quicknote_show_info->enable = 1;
				get_dashboard_state()->quicknote_show_info->started_by_app = 0;
				get_dashboard_state()->quicknote_show_info->action = 7;
				memset(get_dashboard_state()->quicknote_show_info->data, 0, 0x14a);
				update_persist_task_status(get_dashboard_state(), 12, 2);
			}
		} else if (get_dashboard_state()->quicknote_show_info->longpress_req == 2) {
			get_dashboard_state()->quicknote_show_info->longpress_req = 0;
			data[0] = 1;
			data[1] = 0;
			for (i = 1; i < 4; i++) {
				if (sync_to_slave(get_dashboard_state(), 6, data, 2) >= 5000) {
					G1_LOG_WRN(
						"SYNC TO Slave failed %d times,don't exec quick note start function.\n",
						i);
				} else {
					display_DelayClose(10000);
					k_yield();
					return;
				}
			}
		}
	} else {
		uint8_t data[2];

		if (get_dashboard_state()->quicknote_show_info->longpress_req == 1) {
			get_dashboard_state()->quicknote_show_info->longpress_req = 0;
			if (get_dashboard_state()->dashboard_mode == 12) {
				G1_LOG_ERR("quicknote is running, exit this time\n");
				return;
			}
			data[0] = 1;
			data[1] = 0;
			sync_to_master(get_dashboard_state(), 6, data, 2);
		} else if (get_dashboard_state()->quicknote_show_info->longpress_req == 2) {
			get_dashboard_state()->quicknote_show_info->longpress_req = 0;
			data[0] = 0;
			data[1] = 0;
			sync_to_master(get_dashboard_state(), 6, data, 2);
		}
	}
}

static void key_event_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	uint8_t data[2];
	uint8_t mode;
	uint8_t n;
	int i;
	int type;

	init_key_event_sem();
	for (;;) {
		if (get_dashboard_state()->work_mode == 1 ||
		    get_dashboard_state()->work_mode == 8) {
			k_sleep(K_MSEC(5000));
			continue;
		}
		if (k_sem_take(&key_event_sem, K_FOREVER) == 0) {
			G1_LOG_ERR("key event trigger, type %d\n\n", key_event_type);
			type = key_event_type;
			switch (type) {
			case 1:
				if (state->dashboard_ts->dashboard_hold == 1) {
					continue;
				}
				if (state->onboarding->active) {
					report_event_to_phone(1);
					if (state->onboarding->action == 5) {
						if (get_dashboard_state()->is_master != 2) {
							continue;
						}
						data[0] = 0x13;
						if (onboarding_sync_data(data, 1) == 0) {
							state->onboarding->action = 0x13;
						}
					} else if (state->onboarding->action == 0x13) {
						if (get_dashboard_state()->is_master != 2) {
							continue;
						}
						data[0] = 0x14;
						if (onboarding_sync_data(data, 1) == 0) {
							state->onboarding->action = 0x14;
						}
					} else {
						if (state->onboarding->action == 0x14 &&
						    get_dashboard_state()->is_master == 1) {
							state->onboarding->action = 0x15;
						}
					}
					continue;
				}
				if ((state->dash_buf->type >= 7 && state->dash_buf->type <= 11) ||
				    state->horiz_pitch_cfg[0] ||
				    (state->evenai_buf->state == 1 &&
				     state->evenai_buf->sub == 3)) {
					report_event_to_phone(1);
					continue;
				}
				if (state->teleprompter->ready) {
					report_event_to_phone(1);
				}
				if (state->is_master == 1) {
					if (state->dashboard_mode == 6) {
						mode = get_pane_mode();
						data[0] = 2;
						data[1] = 1;
						if (mode == 0) {
							data[1] |= getNotesIndex() << 4;
						} else if (mode == 1) {
							data[1] = 3;
							data[1] |= getStocksIndex() << 4;
						} else if (mode == 2) {
							data[1] = 5;
							data[1] |= getNewsIndex() << 4;
						} else if (mode == 3) {
							data[1] = 7;
							G1_LOG_ERR(
								"master send calendar key single click ,timestamp = %d\n",
								state->dashboard_ts->ts);
							data[1] |= getCalenadrIndex() << 4;
						}
						if (sync_to_slave(state, 6, data, 2) >= 5000) {
							G1_LOG_WRN(
								"SYNC TO Slave failed...,don't exec key function.\n");
						} else {
							display_inputEvent(0, 1);
							k_yield();
						}
					}
				} else if ((state->is_master == 2 && count_messages()) &&
					   ((state->dashboard_mode == 6 &&
					     get_dashboard_state()->imu.mode == 2) ||
					    state->dashboard_mode == 4)) {
					while (send_msg_key_to_master() < 0) {
					}
				}

				break;
			case 2:
				if (state->onboarding->active) {
					report_event_to_phone(0x19);
					continue;
				}
				if (state->dashboard_mode == 7) {
					continue;
				}
				if (state->task_status->task == 0 && state->dashboard_mode == 0 &&
				    state->display_mode != 12) {
					if (state->double_tap_customize == 4) {
						if (state->is_master == 1) {
							data[0] = 0x0a;
							data[1] = 1;
							for (i = 1; i != 4; i++) {
								if (sync_to_slave(state, 6, data,
										  2) >= 3000) {
									G1_LOG_ERR(
										"SYNC TO Slave failed %d times,don't exec double click startup dashboard function.\n",
										i);
								} else {
									G1_LOG_ERR(
										"SEND double click event to slave.\n");
									atomic_or(&dashboard_flags,
										  BIT(1));
									state->double_tap_dashboard_on =
										1;
									break;
								}
							}
						} else {
							for (i = 1; i != 4; i++) {
								if (sync_to_master(state, 7, NULL,
										   0) >= 2000) {
									G1_LOG_ERR(
										"SYNC dashborad startup command to start Slave failed %d times\n",
										i);
								} else {
									G1_LOG_ERR(
										"sync dashborad startup command success,wait for starting\n");
									break;
								}
							}
						}
					} else if (state->double_tap_customize != 0) {
						report_event_to_phone(0x20);
						for (n = 100; n != 0; n--) {
							k_msleep(20);
						}
					}
					continue;
				}
				if (state->dashboard_mode == 6 &&
				    (state->double_tap_customize == 4 ||
				     state->dashboard_ts->dashboard_hold != 0)) {
					report_event_to_phone(0);
					if (state->is_master == 1) {
						data[0] = 0x0a;
						data[1] = 2;
						for (i = 1; i != 4; i++) {
							if (sync_to_slave(state, 6, data, 2) >=
							    3000) {
								G1_LOG_ERR(
									"SYNC TO Slave failed %d times,don't exec double click startup dashboard function.\n",
									i);
							} else {
								G1_LOG_ERR(
									"SEND double click event to slave.\n");
								state->double_tap_dashboard_on = 0;
								atomic_or(&dashboard_flags, BIT(1));
								state->dashboard_ts
									->dashboard_hold = 0;
								break;
							}
						}
					} else {
						for (i = 1; i != 4; i++) {
							if (sync_to_master(state, 7, NULL, 0) >=
							    2000) {
								G1_LOG_ERR(
									"SYNC dashborad startup command to close dashboard Slave failed %d times\n",
									i);
							} else {
								G1_LOG_ERR(
									"sync dashborad startup command success,close dashboard function\n");
								break;
							}
						}
					}
					continue;
				}
				n = 101;
				while (state->suspend_state > 1 && --n) {
					k_msleep(10);
				}
				if (state->dashboard_mode != 12 ||
				    (state->dashboard_mode == 12 &&
				     state->quicknote_show_info->action == 10)) {
					report_event_to_phone(0);
					if (state->is_master == 1) {
						if (state->dashboard_mode == 4) {
							expire_cur_message();
							if (get_dashboard_state()->unread_msg_num) {
								set_msg_show_state(6);
							} else {
								set_msg_show_state(0);
							}
						}
						data[0] = 1;
						data[1] = 0;
						if (state->dashboard_mode != 6) {
							atomic_or(&close_app_flags, BIT(1));
							for (i = 1; i != 4; i++) {
								if (sync_to_slave(state, 8, data,
										  2) >= 3000) {
									G1_LOG_ERR(
										"SYNC TO Slave failed %d times,don't exec double click close app function.\n",
										i);
								} else {
									G1_LOG_ERR(
										"SEND double click event to slave.\n");
									exit_running_task(state, 0);
									break;
								}
							}
						} else if (state->dashboard_mode == 6) {
							sync_to_slave(state, 8, data, 2);
							exit_running_task(state, 0);
							update_persist_task_status_to_idle(state);
						}
					} else if (state->dashboard_mode != 6) {
						sync_to_master(state, 4, NULL, 0);
						if (get_dashboard_state()->dashboard_mode == 4) {
							expire_cur_message();
							if (get_dashboard_state()->unread_msg_num) {
								set_msg_show_state(6);
							} else {
								set_msg_show_state(0);
							}
						}
						exit_running_task(state, 0);
					} else if (state->dashboard_mode == 6) {
						sync_to_master(state, 4, NULL, 0);
						if (get_dashboard_state()->dashboard_mode == 4) {
							expire_cur_message();
							if (get_dashboard_state()->unread_msg_num) {
								set_msg_show_state(6);
							} else {
								set_msg_show_state(0);
							}
						}
						exit_running_task(state, 0);
						update_persist_task_status_to_idle(state);
					}
					break;
				}
				data[0] = 1;
				data[1] = 0;
				for (i = 1; i != 4; i++) {
					if (sync_to_slave(state, 6, data, 2) >= 3000) {
						G1_LOG_WRN(
							"SYNC TO Slave failed %d times,don't exec quick note start function.\n",
							i);
					} else {
						display_DelayClose(10000);
						k_yield();
						break;
					}
				}
				G1_LOG_WRN(
					"Click event does not respond, close Quicknote to prevent exceptions\n");
				break;
			case 3:
				if (state->onboarding->active ||
				    state->dashboard_ts->dashboard_hold == 1) {
					continue;
				}
				if (state->display_mode == 12) {
					report_event_to_phone(5);
					if (state->is_master == 1) {
						if (sync_to_slave(state, 5, NULL, 0) < 3000) {
							show_not_disturb_bitmap(1);
						}
					} else if (sync_to_master(state, 9, NULL, 0) < 2000) {
						show_not_disturb_bitmap(1);
					}
					state->display_mode = 10;
					G1_LOG_ERR(
						"#############################exit not disturb################################\n\n");
				} else {
					report_event_to_phone(4);
					if (state->is_master == 1) {
						sync_to_slave(state, 4, NULL, 0);
						exit_running_task(state, 0);
					} else {
						sync_to_master(state, 8, NULL, 0);
					}
					state->display_mode = 12;
					exit_running_task(state, 1);
					request_not_disturb_bitmap(0);
					G1_LOG_ERR(
						"#############################enter not disturb################################\n\n");
				}
				state->dashboard_cmd = 3;
				k_sem_give(&state->dashboard_position_sem);
				break;
			case 4:
				if (state->onboarding->active ||
				    state->dashboard_ts->dashboard_hold == 1) {
					G1_LOG_ERR(
						"[onboarding/set_lum_coeffic] is running, exit this time\n");
					continue;
				}
				long_press_active = 1;
				if (state->display_mode != 12 &&
				    (is_idle() || state->dashboard_mode == 6 ||
				     state->dashboard_mode == 1)) {
					if (state->is_master == 2) {
						uint8_t ok = get_glasses_ble_status();

						if (ok == 1) {
							state->evenai_buf->sub = 6;
							state->evenai_buf->action_status = 2;
							report_event_to_phone(0x17);
						} else {
							state->evenai_buf->sub = 6;
							state->evenai_buf->action_status = 1;
						}
					} else {
						state->quicknote_show_info->longpress_req = 1;
						check_disp_quicknote();
					}
				} else {
					G1_LOG_WRN(
						"longpress task cannot be started because screen_id %d, %d, %d\n",
						state->dashboard_mode, is_idle(),
						state->display_mode);
				}
				break;
			case 6:
				long_press_active = 0;
				if (state->is_master == 2) {
					if (state->dashboard_mode == 0x10) {
						if (state->dash_buf->type == 4) {
							state->dash_buf->type = 5;
						}
					} else if (state->dashboard_mode == 0x14 &&
						   get_glasses_ble_status() == 1 &&
						   state->evenai_buf->sub == 7 &&
						   state->evenai_buf->action_status == 2) {
						report_event_to_phone(0x18);
					}
				} else if (!lp_timeout_handled && state->dashboard_mode == 12) {
					lp_timeout_handled = 1;
					state->quicknote_show_info->longpress_req = 2;
					check_disp_quicknote();
				}
				break;
			case 5:
				long_press_active = 0;
				if (state->is_master == 2) {
					if (state->dashboard_mode == 0x10) {
						if (state->dash_buf->type == 4) {
							state->dash_buf->type = 5;
						}
					} else if (state->dashboard_mode == 0x14 &&
						   get_glasses_ble_status() == 1 &&
						   state->evenai_buf->sub == 7 &&
						   state->evenai_buf->action_status == 2) {
						report_event_to_phone(0x18);
					}
				} else if (state->dashboard_mode == 12) {
					state->quicknote_show_info->longpress_req = 2;
					check_disp_quicknote();
				}
				break;
			default:
				break;
			}
		}
		key_event_type = 0;
	}
}

static void touch_key_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	uint32_t now, t1;
	int press_t = 0;
	int release_t = 0;
	int clicks = 0;
	int lp_timeout = 0;
	int long_press = 0;
	int hold, since_release, press_dur;
	uint8_t n;

	for (;;) {
		k_sem_take(&state->touch_sem, K_MSEC(250));
		if (state->work_mode == 1 || get_dashboard_state()->work_mode == 8) {
			k_msleep(5000);
			continue;
		}
		if (touch_irq_pending) {
			aw9320x_get_irq_stat();
			touch_irq_pending = 0;
		}
		if (!(state->module_mask & BIT(0))) {
			continue;
		}
		switch (aw9320x_wearing) {
		case IN_EAR:
			now = sys_clock_tick_get_32();
			aw9320x_diff_get(&state->aw_diff);
			if ((int)(now - press_t) > 10000) {
				clicks = 0;
				release_t = 0;
			}
			clicks++;
			aw9320x_wearing = UNKNOWN;
			press_t = now;
			break;
		case OUT_EAR:
			release_t = sys_clock_tick_get_32();
			aw9320x_wearing = UNKNOWN;
			lp_timeout_handled = 0;
			break;
		default:
			break;
		}
		t1 = sys_clock_tick_get_32();
		since_release = sys_clock_tick_get_32() - release_t;
		press_dur = release_t - press_t;
		if (clicks == 0) {
			continue;
		}
		if (clicks == 1 && release_t == 0) {
			/* first press still held: check for a long press */
			hold = t1 - press_t;
			if (!long_press) {
				aw9320x_diff_get(&state->aw_diff);
				if (state->aw_diff > -20000 && state->aw_diff < 20000) {
					clicks = 0;
					continue;
				}
				if (hold > 15000) {
					G1_LOG_ERR(
						"########################turn on the mic and start to speak! holdtime %d\n\n",
						hold);
					key_event_type = 4;
					trigger_key_event();
					long_press = 1;
				}
			}
			if (long_press && hold > (lp_timeout ? 3 : 35) * 30000) {
				G1_LOG_ERR(
					"#############################Long press timeout %d################################\n\n",
					lp_timeout);
				key_event_type = 6;
				lp_timeout = 0;
				long_press = 0;
				clicks = 0;
				trigger_key_event();
				continue;
			}
		}
		/* act once the key was released after the press and stayed released for a while */
		if (press_t >= release_t || since_release <= 10000) {
			continue;
		}
		if (press_dur > 36000) {
			G1_LOG_ERR(
				"#############################Long press end################################\n\n");
			key_event_type = 5;
			trigger_key_event();
			long_press = 0;
			clicks = 0;
		} else if (press_dur > 15000) {
			G1_LOG_ERR(
				"#############################Short press end################################\n\n");
			lp_timeout = 1;
			release_t = 0;
		} else {
			switch (clicks) {
			case 1:
				G1_LOG_ERR(
					"#############################single click %d################################\n\n",
					state->aw_diff);
				/* wait up to 3 x 100 ms for the Even AI scrolling to end */
				for (n = 0; n < 3 && even_ai_scrolling; n++) {
					k_msleep(100);
				}
				key_event_type = 1;
				trigger_key_event();
				break;
			case 2:
				G1_LOG_ERR(
					"#############################double click################################\n\n");
				key_event_type = 2;
				trigger_key_event();
				break;
			case 3:
				G1_LOG_ERR(
					"#############################triple click################################\n\n");
				key_event_type = 3;
				trigger_key_event();
				break;
			case 6:
				bt_unpair(BT_ID_DEFAULT, NULL);
				state->force_bind = 2;
				G1_LOG_ERR(
					"#############################%d click force bind#############################\n\n",
					6);
				break;
			case 15:
				set_band_for_ESB(10);
				G1_LOG_ERR("%d click, use high band for ESB\n\n", 15);
				break;
			case 5:
				if (state->onboarding->active) {
					break;
				}
				G1_LOG_ERR("%d click, reboot now\n\n", 5);
				reset_pmic_on();
				G1_LOG_ERR("reset_pmic_on\n\n");
				reset_pmic_off();
				G1_LOG_ERR("reset_pmic_off\n\n");
				G1_LOG_ERR(
					"sys reboot because touch event, %d click, reboot now\n\n",
					5);
				k_msleep(500);
				system_reboot_delayed();
				break;
			case 10:
				reset_all_usr_data(state, 1);
				break;
			case 11:
				reset_all_usr_data(state, 0);
				break;
			default:
				break;
			}
			long_press = 0;
			clicks = 0;
		}
	}
}

static void init_key_event_sem(void)
{
	if (k_sem_init(&key_event_sem, 0, 5) == 0) {
		key_event_sem_ready = 1;
	}
}

static void trigger_key_event(void)
{
	if (key_event_sem_ready) {
		k_sem_give(&key_event_sem);
	}
}

void check_sw0_status(void)
{
	if (!get_sw0_irq_level()) {
		G1_LOG_WRN("interrupt line low!!!\n");
		if (++sw0_low_count > 3) {
			G1_LOG_WRN("interrupt line abnormal, now reset\n");
			aw93203_init();
		}
	} else {
		sw0_low_count = 0;
	}
}

void input_start(struct GlassesState *state)
{
	if (get_pt_test_mode() == 1) {
		k_thread_create(&touch_key_thread_data, touch_key_stack,
				K_THREAD_STACK_SIZEOF(touch_key_stack),
				product_test_touch_key_thread, state, NULL, NULL, -14, 0,
				K_NO_WAIT);
	} else {
		k_thread_create(&touch_key_thread_data, touch_key_stack,
				K_THREAD_STACK_SIZEOF(touch_key_stack), touch_key_thread, state,
				NULL, NULL, -14, 0, K_NO_WAIT);
	}
	k_thread_create(&key_event_thread_data, key_event_stack,
			K_THREAD_STACK_SIZEOF(key_event_stack), key_event_thread, state, NULL, NULL,
			-13, 0, K_NO_WAIT);
}
