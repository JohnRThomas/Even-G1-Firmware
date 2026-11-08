/*
 * Periodic system monitor thread: battery and charger polling, wear detection
 * and the work-mode changes that follow from it, bonding window, onboarding
 * trigger, event reports to the phone, watchdog feeding.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include "system/monitor.h"
#include "system/logging.h"
#include "system/retained_mem.h"
#include "system/system_state.h"
#include "system/watchdog.h"
#include "system/work_mode.h"
#include "main.h"
#include "comm/ble/ble.h"
#include "comm/ble/report.h"
#include "comm/esb/esb.h"
#include "comm/ipc/netcore_link.h"
#include "drivers/touch/aw9320x.h"
#include "services/nfc_wlc/wlc.h"
#include "services/notifications/notify.h"
#include "system/low_power.h"
#include "services/production_test/production_test.h"
#include "services/storage/storage.h"
#include "services/tasks/tasks.h"
#include "ui/input.h"
#include "ui/screens/dashboard.h"
#include "ui/screens/ui.h"
#include "drivers/ambient/opt3007.h"
#include "services/power/fuel_gauge.h"
#include "drivers/mic/dmic.h"
#include "services/power/charger_box.h"
#include "ui/screens/ui_bitmap.h"
#include "ui/screens/ui_onboarding.h"
#include "services/storage/settings_records.h"
#include "services/production_test/pt_aging.h"

#define CHARGER_DEV DEVICE_DT_GET(DT_NODELABEL(pmic_charger))

uint8_t fuel_gauge_stuck;
uint8_t watchdog_enabled = 1;
static uint8_t is_wearing; /* wear detection state (check_work_mode) */
static uint8_t wear_cnt;
static uint8_t ble_conn_hold_count;
static uint8_t unwear_cnt;
static uint8_t chipid_check_cnt;
static uint8_t box_status_cnt;
static uint8_t charging_cnt;
static uint8_t bind_count;
static uint8_t hci_fail_cnt; /* consecutive HCI sync command failures */
static uint8_t wear_state_event = 0xff; /* last wear/case event sent (6/7/8/0x0b) */
static uint8_t last_bind_state = 0xff;
static uint8_t last_box_soc = 0xff;
static uint8_t last_box_status = 0xff;
static uint8_t last_battery = 0xff;
static uint8_t last_charging = 0xff;
static uint8_t last_brightness = 0xff;

static K_THREAD_STACK_DEFINE(low_speed_stack, 0xc00);
static struct k_thread low_speed_thread_data;
static void check_not_disturb_sync(void)
{
	if (get_dashboard_state()->bitmap_info->sync_pending &&
	    ((get_dashboard_state()->is_master == 1 && is_idle()) ||
	     get_dashboard_state()->is_master == 2)) {
		sync_not_disturb_bitmap(get_dashboard_state()->bitmap_info->mode);
	}
}

static void low_speed_peripheral_dispatch_thread_for_test(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	int dfu_count = 0;

	G1_LOG_WRN("#enter\n");
	state->battery_info[0] = 0;
	state->battery_info[1] = 0;
	state->battery_voltage = 0;
	state->battery_current = 0x80;
	while (1) {
		while (state->work_mode == 1) {
			watchdog_disable();
			set_aging_mode(0);
			dmic_stream_stop();
			if (dfu_count > 34) {
				G1_LOG_ERR("reboot because dfu exceed time\r\n\n");
				k_msleep(500);
				sys_reboot(SYS_REBOOT_COLD);
			}
			dfu_count++;
			k_sleep(K_MSEC(5000));
		}
		nfc_wlc_process();
		if (watchdog_enabled) {
			if (!fuel_gauge_stuck) {
				watchdog_feed();
			}
		} else {
			watchdog_disable();
		}
		fuel_gauge_update(CHARGER_DEV);
		aging_charge_check();
		k_sleep(K_MSEC(1000));
	}
}

uint8_t get_wearing_state(void)
{
	return is_wearing;
}

uint8_t get_wear_state_event(void)
{
	return wear_state_event;
}

static void check_work_mode(int in_box, int lid_closed, uint8_t report)
{
	uint8_t mode = get_work_mode();
	uint8_t limit;
	uint8_t val;
	uint8_t evt;
	bool wearing;

	if (mode == 0 || mode == 8) {
		return;
	}
	if (report && mode == 1) {
		if (last_bind_state == 0xff ||
		    get_dashboard_state()->ble_context.tx_sem.count == 2) {
			if (get_dashboard_state()->ble_context.tx_sem.count == 2) {
				k_sem_take(&get_dashboard_state()->ble_context.tx_sem, K_FOREVER);
			}
			G1_LOG_ERR("EVENT_GLASS_HAS_FINISH_BIND\n");
			report_event_to_phone(0x11);
			last_bind_state = 0xfe;
		}
		return;
	}
	if (k_uptime_get() < 10000) {
		return;
	}
	if (get_dashboard_state()->battery_voltage <= 0x82 &&
	    get_dashboard_state()->battery_voltage > 0x50 &&
	    get_dashboard_state()->battery_current_ma > 0 &&
	    get_dashboard_state()->battery_info[0] < 2 && !in_box) {
		if (get_dashboard_state()->is_master == 1) {
			sync_to_slave(get_dashboard_state(), 3, NULL, 0);
		} else {
			sync_to_master(get_dashboard_state(), 5, NULL, 0);
		}
		exit_running_task(get_dashboard_state(), 1);
		change_work_mode(7);
		return;
	}

	if (esb_signal > 50) {
		is_wearing = 1;
		if (mode != 2 && mode != 3) {
			exit_running_task(get_dashboard_state(), 0);
			change_work_mode(3);
			G1_LOG_ERR("MODE_NORMAL_STANBY_ID\n");
		} else if (is_task_running(get_dashboard_state())) {
			change_work_mode(2);
			G1_LOG_ERR("MODE_NORMAL_RUN_ID\n");
		}
		unwear_cnt = 0;
		val = wear_cnt + 1;
		if (val >= 2) {
			wear_cnt = 2;
			val = chipid_check_cnt + 1;
			if (val < 10) {
				chipid_check_cnt = val;
			} else {
				chipid_check_cnt = 0;
				if (aw9320x_read_chipid()) {
					aw93203_init();
				}
			}
		} else {
			wear_cnt = val;
		}
		wearing = true;
	} else {
		wearing = false;
		if (esb_signal < 40) {
			chipid_check_cnt = 0;
			if (is_task_running(get_dashboard_state())) {
				limit = get_dashboard_state()->onboarding->active ? 10 : 60;
			} else {
				limit = 3;
			}
			val = unwear_cnt + 1;
			if (val < limit) {
				unwear_cnt = val;
			} else {
				unwear_cnt = limit;
				is_wearing = 0;
				wear_cnt = 0;
				if (get_dashboard_state()->double_tap_dashboard_on) {
					get_dashboard_state()->double_tap_dashboard_on = 0;
				}
				if (in_box && lid_closed) {
					exit_running_task(get_dashboard_state(), 1);
					change_work_mode(6);
					G1_LOG_ERR("MODE_CHARGING_LOW_POWER_ID\n");
				} else if (in_box) {
					exit_running_task(get_dashboard_state(), 1);
					change_work_mode(5);
					G1_LOG_ERR("MODE_CHARGING_RUN_ID\n");
				} else if (mode != 4) {
					exit_running_task(get_dashboard_state(), 1);
					change_work_mode(4);
					G1_LOG_ERR("MODE_NORMAL_LOW_POWER_ID\n");
				}
			}
		}
	}
	if (report) {
		if (last_bind_state == 0xff ||
		    get_dashboard_state()->ble_context.tx_sem.count == 2) {
			if (get_dashboard_state()->ble_context.tx_sem.count == 2) {
				k_sem_take(&get_dashboard_state()->ble_context.tx_sem, K_FOREVER);
			}
			G1_LOG_ERR("EVENT_GLASS_HAS_FINISH_BIND\n");
			report_event_to_phone(0x11);
			last_bind_state = 0xfe;
			return;
		}

		if (in_box) {
			if (lid_closed) {
				evt = 0xb;
				G1_LOG_ERR("EVENT_PUT_IN_GLASS_BOX_CLOSE\n");
			} else {
				evt = 8;
				G1_LOG_ERR("EVENT_PUT_IN_GLASS_BOX_OPEN\n");
			}
		} else {
			if (wearing) {
				evt = 6;
				G1_LOG_ERR("EVENT_ENTER_WARED\n");
			} else {
				evt = 7;
				G1_LOG_ERR("EVENT_UNWARED_OUTBOX\n");
			}
			last_box_soc = 0xff;
			last_box_status = 0xff;
			box_status_cnt = 4;
			charging_cnt = 4;
		}
		if (wear_state_event != evt) {
			report_event_to_phone(evt);
			wear_state_event = evt;
			return;
		}

		val = get_dashboard_state()->battery_info[0];
		if (last_battery != val && val <= 100) {
			last_battery = val;
			G1_LOG_ERR("EVENT_STATE_BATTERY_PERCENT %d\n", val);
			report_event_to_phone(10);
			return;
		}

		if (in_box) {
			val = is_box_charging();
			if (val) {
				charging_cnt = 4;
			}
		} else {
			val = 0;
		}
		if (last_charging != val) {
			if (++charging_cnt > 4) {
				charging_cnt = 0;
				get_dashboard_state()->ble_context.charging = val;
				last_charging = val;
				report_event_to_phone(9);
				G1_LOG_ERR("EVENT_STATE_CHARGING %d\n", val);
			}
			return;
		}
		charging_cnt = 0;
		if (in_box) {
			val = get_pt_level_flag();
			if (last_box_status != val) {
				if (++box_status_cnt > 4) {
					box_status_cnt = 0;
					last_box_status = val;
					G1_LOG_ERR("EVENT_GLASS_BOX_STATUS %d\n", val);
					report_event_to_phone(0xe);
				}
				G1_LOG_ERR(
					"last_box_charging_status is %d, box_charging_status is %d\n",
					last_box_status, val);
				return;
			}
			box_status_cnt = 0;
			val = get_pt_level();
			if (last_box_soc != val && val <= 100) {
				last_box_soc = val;
				G1_LOG_ERR("EVENT_GLASS_BOX_SOC %d\n", val);
				report_event_to_phone(0xf);
				return;
			}
			G1_LOG_ERR("last_box_soc is %d, box_soc is %d\n", last_box_soc, val);
		}

		if (get_dashboard_state()->is_master != 1) {
			return;
		}
		if (!get_dashboard_state()->opt3007_context.ready) {
			return;
		}
		if (opt3007_get_brightness_level(&get_dashboard_state()->opt3007_context) == 0xff) {
			return;
		}
		if (get_dashboard_state()->panel.panel_brightness_level == last_brightness) {
			return;
		}
		last_brightness = get_dashboard_state()->panel.panel_brightness_level;
		report_event_to_phone(0x12);
	} else {
		last_bind_state = 0xff;
		last_box_soc = 0xff;
		last_box_status = 0xff;
		last_battery = 0xff;
		last_charging = 0xff;
		last_brightness = 0xff;
		box_status_cnt = 4;
		charging_cnt = 4;
	}
}

static void check_disp_onboarding(int force)
{
	uint8_t data[3];
	int64_t ts;

	if (force != 0 || !is_wearing ||
	    (is_ble_device_bound() && !get_dashboard_state()->onboarding->action)) {
		if (get_dashboard_state()->is_master == 1) {
			onboarding_deactivate();
		}
	} else {
		if ((is_wearing && get_dashboard_state()->task_status->task == 0 &&
		     get_dashboard_state()->dashboard_mode == 0 &&
		     get_dashboard_state()->is_master == 1) &&
		    (!is_ble_device_bound() || get_dashboard_state()->onboarding->in_progress)) {
			if (!is_ble_device_bound()) {
				G1_LOG_WRN("ble device unbound,prepare to show binding tip\n");
				get_dashboard_state()->onboarding->action = 0;
			}
			get_dashboard_state()->onboarding->sub_step = 0;
			onboarding_shown_sub_step = get_dashboard_state()->onboarding->sub_step;
			data[0] = get_dashboard_state()->onboarding->action;
			data[1] = onboarding_shown_sub_step;
			data[2] = 1;
			if (onboarding_sync_data(data, 3) == 0) {
				G1_LOG_ERR("ready to show onboarding...\n");
				onboarding_request_redraw();
				get_dashboard_state()->onboarding->active = 1;
				update_persist_task_status(get_dashboard_state(), 0xe, 2);
				get_dashboard_state()->onboarding->saved_brightness =
					get_dashboard_state()->panel.panel_brightness_level;
				get_dashboard_state()->panel.panel_brightness_level = 42;
				ts = k_uptime_get();
				get_dashboard_state()->onboarding->ts = ts;
				get_dashboard_state()->esb_down_id =
					get_dashboard_state()->onboarding->sub_step;
			}
		}
	}
}

static void check_bind_status(struct GlassesState *state, int touched, int bonded)
{
	if (touched) {
		if (bonded) {
			state->force_bind = 0;
		} else if (!state->force_bind) {
			state->force_bind = 1;
			G1_LOG_ERR("enable allow bind mode\n");
		}
		bind_count = 3;
	} else {
		if (bind_count > 0) {
			bind_count--;
		}
		if (bind_count == 0 && state->force_bind == 1) {
			state->force_bind = 0;
			G1_LOG_ERR("disable allow bind mode\n");
		}
	}
}

uint8_t get_glasses_ble_status(void)
{
	struct GlassesState *s1 = get_dashboard_state();
	struct GlassesState *s2 = get_dashboard_state();
	uint8_t connected = (uintptr_t)get_current_conn();

	if (ble_conn_hold_count == 15 && connected) {
		if (get_dashboard_state()->is_master == 1) {
			if (s2->esb_pkt.flags & 1) {
				s1->sync_flags |= 3;
			}
		} else {
			s2->esb_pkt.flags |= 1;
		}
	}
	return (s1->sync_flags & 3) == 3;
}

const struct device *get_charger_device(void)
{
	return CHARGER_DEV;
}

static void low_speed_peripheral_dispatch_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	struct bt_conn *conn;
	int8_t sec_retry = 0;
	uint8_t cnt = 0;
	int in_box, lid_closed;
	uint8_t connected;
	int err;

	change_work_mode(3);
	update_persist_task_status_to_idle(state);
	G1_LOG_WRN("#enter\n");
	state->battery_voltage = 0;
	state->battery_info[0] = 0;
	state->battery_info[1] = 0;
	state->battery_current = 0x80;
	if (state->is_master == 1 && state->display_mode == 12) {
		sync_to_slave(state, 4, NULL, 0);
	}
	for (;;) {
		cnt++;
		if (cnt % 5 == 0) {
			/* bt_enable() discards commands queued before it, so the controller is
			 * only pinged once the host has finished enabling */
			if (get_dashboard_state()->work_mode != 1 && bt_is_ready()) {
				err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_LOCAL_VERSION_INFO, NULL,
							   NULL);
				if (err) {
					if (++hci_fail_cnt > 2) {
						hci_fail_cnt = 0;
						G1_LOG_ERR(
							"watch dog reset becase bt hci cmd send sync failed\n");
						system_reboot_delayed();
					}
				} else {
					hci_fail_cnt = 0;
				}
			}
			conn = get_current_conn();
			if (conn && state->ble_context.send_enabled &&
			    state->ble_context.security_pending &&
			    k_sem_count_get(&state->ble_context.tx_sem)) {
				if (++sec_retry > 4) {
					bt_conn_set_security(conn, BT_SECURITY_L2);
					sec_retry = -5;
				}
			} else {
				sec_retry = 0;
			}
			state->battery_min = MIN(state->battery_info[0], state->battery_info[1]);
			fuel_gauge_update(CHARGER_DEV);
			in_box = is_in_box();
			lid_closed = is_box_lid_closed();
			connected = conn && !state->ble_context.security_pending &&
				    k_sem_count_get(&state->ble_context.tx_sem);
			check_bind_status(state, in_box, lid_closed);
			if (state->is_master == 1) {
				runtime_info_sync(state);
				if (connected && (state->esb_pkt.flags & BIT(0))) {
					state->sync_flags |= 3;
					ble_conn_hold_count = 15;
				} else if (ble_conn_hold_count) {
					ble_conn_hold_count--;
				} else {
					state->sync_flags &= ~3;
				}
				if (sync_to_slave(state, 0xc, NULL, 0) < 500 &&
				    (state->sync_flags & BIT(2))) {
					clear_all_messages();
					state->sync_flags &= ~BIT(2);
				}
			} else {
				if (connected) {
					ble_conn_hold_count = 15;
					state->esb_pkt.flags |= BIT(0);
				} else if (ble_conn_hold_count) {
					ble_conn_hold_count--;
				} else {
					state->esb_pkt.flags &= ~BIT(0);
				}
				if ((state->sync_flags ^ state->esb_pkt.flags) & BIT(0)) {
					state->esb_pkt.data[0]++;
				}
				if (sync_to_master(state, 1, NULL, 0) < 500 &&
				    (state->esb_pkt.flags & BIT(2))) {
					clear_all_messages();
					state->esb_pkt.flags &= ~BIT(2);
					state->sync_flags &= ~BIT(2);
				}
			}
			nfc_wlc_process();
			while (state->work_mode == 1) {
				if (k_uptime_get() - state->last_sync_ts >= 600000) {
					G1_LOG_ERR("reboot because dfu exceed time\n");
					system_reboot_delayed();
					k_msleep(500);
					continue;
				}
				watchdog_disable();
				if (state->is_master == 1) {
					update_dashboard_clock(state,
							       (uint32_t *)state->dashboard_ts);
				}
				G1_LOG_ERR("********************DFU MODE********************\n");
				check_work_mode(in_box, lid_closed, connected);
				k_sleep(K_MSEC(1000));
			}
			if (watchdog_enabled) {
				if (!fuel_gauge_stuck) {
					watchdog_feed();
				}
			} else {
				watchdog_disable();
			}
			if (state->is_master == 1) {
				update_dashboard_clock(state, (uint32_t *)state->dashboard_ts);
			}
			if (getEraseDFU() && k_uptime_get() >= 60000) {
				erase_dfu_flash();
				setEraseDFU(false);
			}
			G1_LOG_WRN(
				"Global Working Mode: %d get_glasses_ble_status %d ble_is_connected %d\n",
				state->display_mode, get_glasses_ble_status(), connected);
			try_enter_low_power_mode();
			check_work_mode(in_box, lid_closed, connected);
			check_disp_onboarding(in_box);
			/* Remove the 30s timeout for android devices */
			if (conn && false /* !state->phone_is_ios */ &&
			    (uint32_t)(k_uptime_get() - state->ble_context.last_rx_time) > 30000 &&
			    state->work_mode != 1) {
				state->ble_context.last_rx_time = k_uptime_get();
				bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			}
			if (state->user_data_reset_pending) {
				state->user_data_reset_pending = 0;
				exit_running_task(get_dashboard_state(), 0);
				reset_all_usr_data(state, 1);
			}
			check_sw0_status();
			cnt = 0;
		}
		check_not_disturb_sync();
		if (!fuel_gauge_stuck) {
			watchdog_feed();
		}
		k_sleep(K_MSEC(200));
	}
}

void monitor_start(struct GlassesState *state)
{
	if (!get_pt_test_mode()) {
		k_thread_create(&low_speed_thread_data, low_speed_stack,
				K_THREAD_STACK_SIZEOF(low_speed_stack),
				low_speed_peripheral_dispatch_thread, state, NULL, NULL, -10, 0,
				K_NO_WAIT);
	} else {
		k_thread_create(&low_speed_thread_data, low_speed_stack,
				K_THREAD_STACK_SIZEOF(low_speed_stack),
				low_speed_peripheral_dispatch_thread_for_test, state, NULL, NULL,
				-10, 0, K_NO_WAIT);
	}
}
