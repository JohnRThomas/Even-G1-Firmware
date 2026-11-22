/*
 * nPM1300 fuel gauge / charger helpers and charging-box status, derived from
 * the NCS npm1300_fuel_gauge sample (src/fuel_gauge.c).
 */
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "nrf_fuel_gauge.h"

#include "../aw9320x/aw9320x.h"
#include "../ble/ble_cmd.h"
#include "../production_test/production_test.h"
#include "../storage/storage.h"
#include "../threads/threads.h"
#include "g1_log.h"
#include "glasses_state.h"
#include "sensors_audio.h"

static int64_t fuel_gauge_ref_time;
static float fg_last_soc;
static uint32_t fg_last_soc_time;

static const struct battery_model battery_model = {
#include "battery_model.inc"
};

static uint8_t zero_current_count;
static bool fuel_gauge_ready;          /* the library was initialised */

static int read_sensors(const struct device *charger, float *voltage, float *current, float *temp) {
  struct sensor_value value;
  int ret;

  ret = sensor_sample_fetch(charger);
  if (ret < 0) {
    return ret;
  }

  sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
  *voltage = (float)value.val1 + ((float)value.val2 / 1000000);

  sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
  *temp = (float)value.val1 + ((float)value.val2 / 1000000);

  sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
  *current = (float)value.val1 + ((float)value.val2 / 1000000);

  return 0;
}

int fuel_gauge_init(const struct device *charger) {
  struct sensor_value value;
  struct nrf_fuel_gauge_init_parameters parameters = {.model = &battery_model};
  int ret;

  ret = read_sensors(charger, &parameters.v0, &parameters.i0, &parameters.t0);
  if (ret < 0) {
    return ret;
  }

  sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);

  ret = nrf_fuel_gauge_init(&parameters, NULL);
  fuel_gauge_ready = ret >= 0;
  if (ret < 0) {
    return ret;
  }

  fuel_gauge_ref_time = k_uptime_get();

  return 0;
}

bool is_old_board(void) {
  const char *hw = get_glasses_name();

  if ((hw[4] == 'E') && (hw[5] == 'G') && (hw[6] == 'P')) {
    if (hw[7] == '0') {
      return true;
    }
    if (hw[7] == '1') {
      return (hw[8] >= '0') && (hw[8] <= '3');
    }
  }
  return false;
}

int fuel_gauge_update(const struct device *charger) {
  float voltage;
  float current;
  float temp;
  float soc = 0.0f;
  float new_soc;
  int retry = 11;
  uint32_t now;

  if (!fuel_gauge_ready) {
    return -ENODEV;
  }

  if (pt_comm_busy_get()) {
    return 0;
  }
  while (1) {
    lock_sensor_mutex();
    int ret = read_sensors(charger, &voltage, &current, &temp);
    unlock_sensor_mutex();
    if (ret < 0) {
      printk("Error: Could not read from charger device\n");
      return ret;
    }

    /* 1 s after the previous update; the retries (soc > 0) follow 5 ms apart */
    int64_t delta = k_uptime_get() - fuel_gauge_ref_time;
    if ((delta >= 1000) || (soc > 0.0f)) {
      soc = nrf_fuel_gauge_process(voltage, current, temp, (float)delta / 1000.f, NULL);
      fuel_gauge_ref_time += delta;
      if ((current != 0.0f) || (--retry == 0) || !(soc < 100.0f) || !(soc > 0.0f)) {
        break;
      }
      k_sleep(K_MSEC(5));
    } else {
      return -1;
    }
    if (pt_comm_busy_get()) {
      break;
    }
  }

  G1_LOG_ERR("V: %.3f, I: %.3f, T: %.2f\n", voltage, current, temp);

  now = k_uptime_get();
  if (((soc != 0.0f) && (fg_last_soc != soc)) || (soc >= 100.0f)) {
    fg_last_soc_time = now;
    fg_last_soc = soc;
  } else if ((now - fg_last_soc_time) >= 120000U) {
    fuel_gauge_stuck = 1;
  } else {
    /* nothing to do */
  }

  if (is_old_board()) {
    new_soc = soc * 1.25f;
  } else {
    new_soc = soc;
  }
  G1_LOG_ERR("Original soc %.2f, New Soc %.2f, %s\n", soc, new_soc, get_glasses_name());

  if ((new_soc < 0.0f) || (new_soc >= 150.0f)) {
    new_soc = 0.0f;
  } else if (new_soc >= 100.0f) {
    new_soc = 100.0f;
  } else {
    /* nothing to do */
  }
  if (((new_soc >= 93.5f) && (new_soc < 94.0f)) || ((new_soc >= 94.5f) && (new_soc < 95.0f))) {
    get_dashboard_state()->battery_soc_flag = 1;
  } else {
    get_dashboard_state()->battery_soc_flag = 0;
  }
  get_dashboard_state()->battery_left = (uint8_t)new_soc;
  get_dashboard_state()->battery_voltage = (uint8_t)((voltage * 100.0f) - 200.0f);

  if (current != 0.0f) {
    get_dashboard_state()->battery_current = (uint8_t)((current + 0.128) * 1000.0);
    get_dashboard_state()->battery_current_ma = (int8_t)(current * 1000.0f);
    zero_current_count = 0;
  } else if (++zero_current_count > 5) {
    zero_current_count = 0;
    get_dashboard_state()->battery_current = (uint8_t)((current + 0.128) * 1000.0);
    get_dashboard_state()->battery_current_ma = (int8_t)(current * 1000.0f);
  } else {
    /* nothing to do */
  }

  get_dashboard_state()->battery_temp = (uint8_t)temp;
  get_dashboard_state()->battery_temp_x100 = (int16_t)(temp * 100.0f);

  if ((((temp >= 60.0f) && (temp < 120.0f)) || ((temp >= -50.0f) && (temp <= -20.0f))) &&
      (get_dashboard_state()->display_mode != DISPLAY_MODE_NOT_DISTURB)) {
    report_event_to_phone(BLE_EVT_NOT_DISTURB_ENTER);
    if (get_dashboard_state()->is_master == 1) {
      sync_to_slave(get_dashboard_state(), SYNC_CMD_NOT_DISTURB_ENTER, NULL, 0);
      exit_running_task(get_dashboard_state(), 0);
    } else {
      sync_to_master(get_dashboard_state(), SLAVE_CMD_NOT_DISTURB_ENTER, NULL, 0);
    }
    get_dashboard_state()->display_mode = DISPLAY_MODE_NOT_DISTURB;
    exit_running_task(get_dashboard_state(), 1);
    G1_LOG_ERR("#############################Abnormal "
               "temperature################################\n\n");
  }
  return 0;
}
