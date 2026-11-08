/*
 * Storage module private declarations (settings glue, flash settings,
 * file storage thread, setting / quick-note stores).
 */
#ifndef STORAGE_H_
#define STORAGE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "system/glasses_state.h"

/* System (factory) settings, flash 0x130000 */
struct sys_settings {
	uint32_t valid;              /* == 1 */
	uint32_t aging_discharge_time_s;
	uint8_t esb_channel;
	uint32_t imu_base;
	uint32_t lux_base_max;
	uint8_t panel_voltage;
	char sn[32];
	char name[32];
	uint8_t esb_master_addr;
	uint8_t esb_slave_addr;
	uint8_t aging_test_count;
	uint8_t aging_start_soc;
	uint32_t aging_charge_time_s;
	uint16_t canvas_x;
	uint16_t canvas_y;
	uint32_t lux_base[3];
	uint32_t lux_base_spare;
	uint16_t lum_base[3];
	uint32_t luminance_curve;
	uint32_t luminance_curve_a1;
	struct triple aging_discharge_step_s;
	uint32_t aging_elapsed_s;
	uint8_t aging_touch_count;
	uint8_t aging_charge_check_result;
};

/* User settings, flash 0x134000 */
struct usr_settings {
	uint8_t display_mode;
	uint8_t brightness_level;
	uint8_t wakeup_angle;
	uint8_t dof3_enable;
	uint32_t imu_div;
	uint8_t auto_brightness;
	uint8_t bonded;
	uint8_t check_mode;
	uint8_t raster_height_gear;
	struct pk7 bonded_addr;
	uint8_t imu_wear;
	uint8_t message_mode;
	uint8_t charge_limit_upper;
	uint8_t charge_limit_lower;
	uint8_t test_mode;
	uint8_t canvas_distance_gear;
	uint8_t message_stay_time;
	uint8_t double_tap_customize;
	uint32_t user_horiz_pitch;
	uint8_t date_format;
	uint16_t brightness_percent;
	struct u8pair longpress_head_up_mic;
	uint8_t reserved[6];
};

#define SYS_SETTINGS_ADDR 0x130000
#define USR_SETTINGS_ADDR 0x134000

#define FLASH_DEV             DEVICE_DT_GET(DT_NODELABEL(mx25r64))
#define EXT_FLASH_SECTOR_SIZE 4096

/* QuickNote context behind GLOBAL_STATE->quicknote */
#define QUICKNOTE_TITLE_LEN 0x29
#define QUICKNOTE_TEXT_LEN  0x119

/* setting_store.c record (flash) and CURRENT_LANGUAGE */
struct app_setting {
	uint8_t magic; /* flash record: SETTING_MAGIC; CURRENT_LANGUAGE: language set by the phone
			*/
	uint8_t language;
	uint8_t startup_mode;
};

#endif /* STORAGE_H_ */
