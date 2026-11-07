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
#include <glasses_state.h>

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
BUILD_ASSERT(sizeof(struct sys_settings) == 0x98);
BUILD_ASSERT(offsetof(struct sys_settings, lux_base) == 0x64);

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
BUILD_ASSERT(sizeof(struct usr_settings) == 0x2c);

#define SYS_SETTINGS_ADDR   0x130000
#define USR_SETTINGS_ADDR   0x134000

#define FLASH_DEV DEVICE_DT_GET(DT_NODELABEL(mx25r64))

/* QuickNote context behind GLOBAL_STATE->quicknote */
#define QUICKNOTE_TITLE_LEN 0x29
#define QUICKNOTE_TEXT_LEN  0x119

/* setting_store.c record (flash) and CURRENT_LANGUAGE */
struct app_setting {
	uint8_t magic;          /* flash record: SETTING_MAGIC; CURRENT_LANGUAGE: language set by the phone */
	uint8_t language;
	uint8_t startup_mode;
};

/* flash_settings.c */
int local_store_write(const char *key, const void *val, size_t len);
void sett_init(struct local_store *st);
int flash_settings_read(uint32_t addr, void *buf, size_t len);
int flash_settings_write_and_verify(uint32_t addr, const void *data, size_t len);
int write_whitelist_to_flash(uint32_t addr, const uint8_t *data, uint32_t size);
int write_font(uint32_t addr, const void *data, size_t len);
int calc_flash_crc(uint16_t *crc, int addr, uint32_t size);

/* settings_records.c */
extern char SERIAL_NUMBER[32];
extern char glasses_name[32];
int save_sys_settings(struct GlassesState *gs, int force);
int load_sys_setting(struct GlassesState *gs);
int save_usr_settings(struct GlassesState *gs, int unused);
int clear_user_settings(struct GlassesState *gs);
int load_usr_setting(struct GlassesState *gs);
void reset_all_usr_data(struct GlassesState *gs, int reboot);
int get_settings_from_flash(struct GlassesState *gs);
char *get_serial_number(void);
char *get_glasses_name(void); /* hardware version string */
int save_serial_number(const char *sn);
int dump_serial_number(const char *name);
int read_sys_settting_from_flash(struct sys_settings *out);
int read_usr_settting_from_flash(struct usr_settings *out);
int load_settings_from_flash(struct GlassesState *gs);

/* file_storage.c */
extern uint8_t sys_settings_dirty;
extern struct k_sem flash_ops_sem;
extern uint32_t LAST_FILE_CRC;
int enqueue_file(const void *data, size_t len);
uint32_t crc32(uint32_t crc, const uint8_t *buf, int len);
void storage_commit_request(int cmd, uint8_t sys_dirty);
void start_flash_storage_threads(struct GlassesState *gs);

/* setting_store.c */
extern struct app_setting CURRENT_LANGUAGE;
int clearSettingStorageQueue(void);
int upgradeAppLanguageInfoToFlash(uint8_t language);
int cleanAppLanguageInfo(void);
int upgradeDashboardStartupModeInfoToFlash(uint8_t mode);
int cleanDashBoardStartUpModeInfo(void);
int upgradeDoubleTapCustomizeToFlash(uint32_t mode);
int upgradeLongpressTapCustomizeToFlash(uint32_t mode);
uint8_t getAppLanguageInfofromFlash(void);
uint8_t getDashboardStartUpModeInfofromFlash(void);
uint8_t get_current_language(void);
uint8_t get_dashboard_startup_mode(void);
int getSettingStorageQueueNum(void);
int SettingStoreHandler(void);

/* quicknote_store.c */
int clearQuickNoteQueue(void);
int upgradeQuickNoteDataToFlash(int index);
int deleteQuickNoteData(int index);
int getQuickNoteDataFromFlash(uint32_t index, void *title, int title_len, void *text,
			      int text_len);
int getQuickNoteQueueNum(void);
int QuickNoteStoreHandler(void);

#endif /* STORAGE_H_ */
