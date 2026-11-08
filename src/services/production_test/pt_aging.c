/*
 * Production-test aging run: cycles the battery between charging and
 * discharging in the case and records the times, the charger retry and the
 * charge check that go with it.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "services/production_test/production_test.h"
#include "services/production_test/pt_aging.h"
#include "services/production_test/pt_internal.h"
#include "system/logging.h"
#include "system/clock.h"
#include "system/monitor.h"
#include "system/work_mode.h"
#include "main.h"
#include "services/power/fuel_gauge.h"
#include "services/power/charger_box.h"
#include "services/storage/storage.h"
#include "services/tasks/tasks.h"
#include "drivers/display/panel.h"
#include "ui/input.h"
#include "ui/screens/demo_image.h"
#include "drivers/display/jbd_panel.h"
#include "services/storage/settings_records.h"
#include "services/production_test/pt_touch.h"
#include "drivers/pmic/npm1300.h"

static struct k_thread aging_thread;
static int64_t discharge_test_start_ms;
static uint32_t charger_retry_last_ms; /* enable_charger_retry rate limit (ms) */
static uint8_t aging_phase;            /* 1 = charging, 2 = discharging */
static uint8_t aging_mode;
static uint8_t aging_thread_started;
static uint8_t charger_retry_fail_count;
static bool charger_enable_ok = true;

static K_THREAD_STACK_DEFINE(aging_thread_stack, 1024);

bool charger_enable_is_ok(void)
{
	return charger_enable_ok;
}
void enable_charger_retry(void)
{
	uint8_t i;
	uint32_t now;

	for (i = 0; i < 10; i++) {
		if (set_charger_status() == 0) {
			break;
		}
		enable_charger();
		k_sleep(K_MSEC(20));
	}

	now = k_uptime_get();
	if (now - charger_retry_last_ms >= 1000) {
		charger_retry_last_ms = now;
		if (i != 10) {
			charger_retry_fail_count = 0;
			charger_enable_ok = 1;
		} else if (++charger_retry_fail_count > 3) {
			charger_enable_ok = 0;
			charger_retry_fail_count = 3;
		} else {
			charger_enable_ok = 1;
		}
	}
}

uint8_t get_aging_charge_limit(void)
{
	if (aging_mode == 1 && aging_phase != 1) {
		return (aging_phase == 2) ? 70 : 85;
	}

	return 100;
}

uint8_t get_aging_phase(void)
{
	return aging_phase;
}

void set_aging_mode(uint8_t v)
{
	aging_mode = v;
}

uint8_t get_aging_mode(void)
{
	return aging_mode;
}

void enter_aging_mode(void)
{
	exit_silent_mode();
	aging_mode = 1;
	aging_phase = 1;
	get_dashboard_state()->panel.panel_brightness_level = 1;
	set_brightness_to_panel_reg_in_running(get_dashboard_state()->panel.panel_brightness_level);
	get_dashboard_state()->aging_test_count++;
	get_dashboard_state()->aging_start_soc = get_dashboard_state()->battery_left;
	get_dashboard_state()->aging_start_uptime_s = k_uptime_get() / 1000;
	get_dashboard_state()->aging_charge_time_s = 0;
	get_dashboard_state()->aging_discharge_time_s = 0;
	memset(get_dashboard_state()->aging_discharge_step_s, 0,
	       sizeof(get_dashboard_state()->aging_discharge_step_s));
	get_dashboard_state()->aging_touch_count = 0;
	get_dashboard_state()->aging_charge_check_count = 0;
	get_dashboard_state()->aging_charge_check_result = 0;
	get_dashboard_state()->aging_no_charge_count = 0;
	save_sys_settings(get_dashboard_state(), 0);
}

void exit_aging_mode(void)
{
	enter_silent_mode();
	aging_mode = 0;
	aging_phase = 0;
	get_dashboard_state()->panel.panel_brightness_level = 0x15;
	set_brightness_to_panel_reg_in_running(get_dashboard_state()->panel.panel_brightness_level);
}

void aging_charge_test(int interval)
{
	bool need_save;
	int t;
	int i;

	if (aging_mode != 1) {
		return;
	}

	get_dashboard_state()->aging_elapsed_s =
		k_uptime_get() / 1000 - get_dashboard_state()->aging_start_uptime_s;
	if (get_dashboard_state()->aging_elapsed_s <= 900) {
		need_save = get_dashboard_state()->aging_elapsed_s % 60 < interval;
	} else {
		need_save = get_dashboard_state()->aging_elapsed_s % 900 < interval;
	}

	if (aging_phase == 1) {
		if (get_dashboard_state()->aging_elapsed_s >= 300 &&
		    (get_dashboard_state()->panel.init_done == 1 ||
		     get_dashboard_state()->dashboard_mode == 3 ||
		     get_dashboard_state()->suspend_state == 0 || line_buf != 0)) {
			enter_silent_mode();
		}
		if (get_dashboard_state()->battery_left > 96) {
			aging_phase = 2;
			get_dashboard_state()->panel.panel_brightness_level = 0x15;
			set_brightness_to_panel_reg_in_running(
				get_dashboard_state()->panel.panel_brightness_level);
			discharge_test_start_ms = k_uptime_get();
			get_dashboard_state()->aging_charge_time_s =
				k_uptime_get() / 1000 - get_dashboard_state()->aging_start_uptime_s;
			need_save = true;
		}
	} else if (aging_phase == 2) {
		if (get_dashboard_state()->panel.init_done != 1 ||
		    get_dashboard_state()->dashboard_mode != 3 ||
		    get_dashboard_state()->suspend_state == 1 || line_buf == 0) {
			exit_silent_mode();
		}
		if (get_dashboard_state()->battery_left <= 70) {
			t = (k_uptime_get() - discharge_test_start_ms) / 1000;
			for (i = 0; i < 5; i++) {
				t -= get_dashboard_state()->aging_discharge_step_s[i];
			}
			get_dashboard_state()->aging_discharge_step_s[5] = t;
			if (discharge_test_start_ms < k_uptime_get()) {
				get_dashboard_state()->aging_discharge_time_s =
					(k_uptime_get() - discharge_test_start_ms) / 1000;
			} else {
				G1_LOG_ERR(
					"Err:current time is smaller than the discharge test start time!\n");
			}
			aging_phase = 0;
			need_save = true;
		} else if (get_dashboard_state()->battery_left <= 75 &&
			   get_dashboard_state()->aging_discharge_step_s[4] == 0) {
			t = (k_uptime_get() - discharge_test_start_ms) / 1000;
			for (i = 0; i < 4; i++) {
				t -= get_dashboard_state()->aging_discharge_step_s[i];
			}
			get_dashboard_state()->aging_discharge_step_s[4] = t;
			need_save = true;
		} else if (get_dashboard_state()->battery_left <= 80 &&
			   get_dashboard_state()->aging_discharge_step_s[3] == 0) {
			t = (k_uptime_get() - discharge_test_start_ms) / 1000;
			for (i = 0; i < 3; i++) {
				t -= get_dashboard_state()->aging_discharge_step_s[i];
			}
			get_dashboard_state()->aging_discharge_step_s[3] = t;
			need_save = true;
		} else if (get_dashboard_state()->battery_left <= 85 &&
			   get_dashboard_state()->aging_discharge_step_s[2] == 0) {
			t = (k_uptime_get() - discharge_test_start_ms) / 1000;
			for (i = 0; i < 2; i++) {
				t -= get_dashboard_state()->aging_discharge_step_s[i];
			}
			get_dashboard_state()->aging_discharge_step_s[2] = t;
			need_save = true;
		} else if (get_dashboard_state()->battery_left <= 90 &&
			   get_dashboard_state()->aging_discharge_step_s[1] == 0) {
			t = (k_uptime_get() - discharge_test_start_ms) / 1000;
			t -= get_dashboard_state()->aging_discharge_step_s[0];
			get_dashboard_state()->aging_discharge_step_s[1] = t;
			need_save = true;
		} else if (get_dashboard_state()->battery_left <= 95 &&
			   get_dashboard_state()->aging_discharge_step_s[0] == 0) {
			get_dashboard_state()->aging_discharge_step_s[0] =
				(k_uptime_get() - discharge_test_start_ms) / 1000;
			need_save = true;
		}
	} else {
		if (get_dashboard_state()->panel.init_done != 1 ||
		    get_dashboard_state()->dashboard_mode != 3 ||
		    get_dashboard_state()->suspend_state == 1 || line_buf == 0) {
			exit_silent_mode();
		}
	}

	if (need_save) {
		save_sys_settings(get_dashboard_state(), 0);
	}
}

static void aging_test_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		if (aging_mode == 1) {
			G1_LOG_WRN("$$$$$$$$$$$$$$$$$$$$$enter agemode!$$$$$$$$$$$$$$$$$$$$$\n");
			while (aging_mode == 1) {
				demo_image_select(0);
				k_msleep(3000);
				demo_image_select(2);
				k_msleep(1000);
				aging_charge_test(4);
			}
		} else if (aging_mode == 2) {
			demo_image_next();
			aging_mode = 0;
		}
		check_sw0_status();
		k_msleep(100);
	}
}

void aging_charge_check(void)
{
	bool need_save = false;

	if (aging_mode != 1 || aging_phase != 1) {
		return;
	}
	if (get_dashboard_state()->aging_elapsed_s >= 300) {
		return;
	}
	if (get_box_status_by_type(0) != 1) {
		return;
	}
	if (get_dashboard_state()->battery_left > 94) {
		return;
	}

	get_dashboard_state()->aging_charge_check_count++;
	if (get_dashboard_state()->battery_current_ma >= 0) {
		if (get_dashboard_state()->aging_no_charge_count <= 20) {
			get_dashboard_state()->aging_no_charge_count++;
		}
	} else {
		if (get_dashboard_state()->aging_charge_check_result != 2) {
			get_dashboard_state()->aging_no_charge_count = 0;
		}
	}

	if (get_dashboard_state()->aging_charge_check_count > 60 &&
	    get_dashboard_state()->aging_charge_check_result == 0) {
		get_dashboard_state()->aging_charge_check_result = 1;
		need_save = true;
	}
	if (get_dashboard_state()->aging_no_charge_count > 20 &&
	    get_dashboard_state()->aging_charge_check_result != 2) {
		get_dashboard_state()->aging_charge_check_result = 2;
		need_save = true;
	}
	if (need_save) {
		save_sys_settings(get_dashboard_state(), 0);
	}
}

void start_aging_mode_thread(void)
{
	if (aging_thread_started) {
		printk("[%s-%d], thread has started !\n", __func__, __LINE__);
		return;
	}

	if (k_thread_create(&aging_thread, aging_thread_stack,
			    K_THREAD_STACK_SIZEOF(aging_thread_stack), aging_test_thread, NULL,
			    NULL, NULL, -12, 0, K_NO_WAIT) == NULL) {
		aging_thread_started = 1;
	}
}
