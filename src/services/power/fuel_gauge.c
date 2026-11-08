/*
 * Battery state of charge: feeds the nPM1300 battery measurements through the
 * nRF fuel gauge library, publishes the result in GlassesState and reacts to an
 * abnormal battery temperature.  Derived from the NCS npm1300_fuel_gauge sample.
 */
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

#include "nrf_fuel_gauge.h"

#include "system/logging.h"
#include "services/power/fuel_gauge.h"
#include "drivers/pmic/npm1300.h"
#include "drivers/touch/aw9320x.h"
#include "services/production_test/production_test.h"
#include "services/storage/storage.h"
#include "system/monitor.h"
#include "comm/esb/esb.h"
#include "comm/ble/report.h"
#include "services/tasks/tasks.h"
#include "services/storage/settings_records.h"
#include "services/production_test/pt_comm.h"
#include "services/production_test/pt_data.h"

#define DISPLAY_MODE_ABNORMAL_TEMP 0x0c

static int64_t fuel_gauge_ref_time;
static float fg_last_soc;
static uint32_t fg_last_soc_time;

static const struct battery_model battery_model = {
#include "services/power/battery_model.inc"
};

static uint8_t zero_current_count;

int fuel_gauge_init(const struct device *charger)
{
	struct sensor_value value;
	struct nrf_fuel_gauge_init_parameters parameters = {.model = &battery_model};
	int ret;

	ret = npm1300_read_battery(charger, &parameters.v0, &parameters.i0, &parameters.t0);
	if (ret < 0) {
		return ret;
	}

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);

	ret = nrf_fuel_gauge_init(&parameters, NULL);
	if (ret < 0) {
		return ret;
	}

	fuel_gauge_ref_time = k_uptime_get();

	return 0;
}

bool is_old_board(void)
{
	const char *hw = get_glasses_name();

	if (hw[4] == 'E' && hw[5] == 'G' && hw[6] == 'P') {
		if (hw[7] == '0') {
			return true;
		}
		if (hw[7] == '1') {
			return hw[8] >= '0' && hw[8] <= '3';
		}
	}
	return false;
}

int fuel_gauge_update(const struct device *charger)
{
	float voltage = 0;
	float current = 0;
	float temp = 0;
	float soc = 0;
	float new_soc;
	int retry = 11;
	int64_t delta;
	uint32_t now;
	int ret;
	int busy = 0;

	while (1) {
		if (pt_comm_busy_get()) {
			if (soc > 0) {
				busy = 1;
				break;
			}
			return 0;
		}
		lock_sensor_mutex();
		ret = npm1300_read_battery(charger, &voltage, &current, &temp);
		unlock_sensor_mutex();
		if (ret < 0) {
			printk("Error: Could not read from charger device\n");
			return ret;
		}

		delta = k_uptime_get() - fuel_gauge_ref_time;
		if (delta >= 1000) {
			soc = nrf_fuel_gauge_process(voltage, current, temp, (float)delta / 1000.f,
						     NULL);
			if (current != 0 || --retry == 0 || !(soc < 100) || !(soc > 0)) {
				break;
			}
			k_sleep(K_MSEC(5));
		} else {
			return -1;
		}
	}
	if (!busy) {
		fuel_gauge_ref_time = k_uptime_get();
	}

	G1_LOG_ERR("V: %.3f, I: %.3f, T: %.2f\n", voltage, current, temp);

	now = k_uptime_get();
	if ((soc != 0 && fg_last_soc != soc) || soc >= 100) {
		fg_last_soc_time = now;
		fg_last_soc = soc;
	} else if (now - fg_last_soc_time >= 120000) {
		fuel_gauge_stuck = 1;
	}

	if (is_old_board()) {
		new_soc = soc * 1.25f;
	} else {
		new_soc = soc;
	}
	G1_LOG_ERR("Original soc %.2f, New Soc %.2f, %s\n", soc, new_soc, get_glasses_name());

	if (new_soc < 0 || new_soc >= 150) {
		new_soc = 0;
	} else if (new_soc >= 100) {
		new_soc = 100;
	}
	if ((new_soc >= 93.5f && new_soc < 94.0f) || (new_soc >= 94.5f && new_soc < 95.0f)) {
		get_dashboard_state()->battery_soc_flag = 1;
	} else {
		get_dashboard_state()->battery_soc_flag = 0;
	}
	get_dashboard_state()->battery_left = new_soc;
	get_dashboard_state()->battery_voltage = voltage * 100 - 200;

	if (current != 0) {
		get_dashboard_state()->battery_current = (current + 0.128) * 1000.0;
		get_dashboard_state()->battery_current_ma = current * 1000;
		zero_current_count = 0;
	} else if (++zero_current_count > 5) {
		zero_current_count = 0;
		get_dashboard_state()->battery_current = (current + 0.128) * 1000.0;
		get_dashboard_state()->battery_current_ma = current * 1000;
	}

	get_dashboard_state()->battery_temp = temp;
	get_dashboard_state()->battery_temp_x100 = temp * 100;

	if ((temp >= 60 && temp < 120) || (temp >= -50 && temp <= -20)) {
		if (get_dashboard_state()->display_mode != DISPLAY_MODE_ABNORMAL_TEMP) {
			report_event_to_phone(4);
			if (get_dashboard_state()->is_master == 1) {
				sync_to_slave(get_dashboard_state(), 4, NULL, 0);
				exit_running_task(get_dashboard_state(), 0);
			} else {
				sync_to_master(get_dashboard_state(), 8, NULL, 0);
			}
			get_dashboard_state()->display_mode = DISPLAY_MODE_ABNORMAL_TEMP;
			exit_running_task(get_dashboard_state(), 1);
			G1_LOG_ERR("#############################Abnormal "
				   "temperature################################\n\n");
		}
	}
	return 0;
}
