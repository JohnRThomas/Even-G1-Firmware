/*
 * Settings storage on the external flash: the Zephyr settings glue
 * ("ar_glass_userdata_settings" subtree and the local store backend used
 * through GLOBAL_STATE->store), then the raw flash helpers used by the settings
 * records, the white list and the font download (read, write and verify,
 * CRC).  The records themselves are in settings_records.c.
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/settings/settings.h>
#include "system/logging.h"
#include "services/storage/storage.h"
#include "services/storage/flash_settings.h"
#include "services/notifications/whitelist.h"
#include "util/common_utils.h"
#include "system/work_mode.h"
#include "system/monitor.h"
#include "system/watchdog.h"
#include "util/crc.h"

#define SETT_ROOT "ar_glass_userdata_settings"

static void local_store_delete(const char *key)
{
	char path[128] = SETT_ROOT "/";

	strcat(path, key);
	settings_delete(path);
}

int local_store_write(const char *key, const void *val, size_t len)
{
	char path[128] = SETT_ROOT "/";
	int err;

	strcat(path, key);
	err = settings_save_one(path, val, len);
	if (err) {
		G1_LOG_ERR("failed to store key: %s --- %d\n", key, len);
	} else {
		G1_LOG_INF("save success:[%s], len=%d\n", key, len);
	}
	return 0;
}

static int sett_handler(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	int rc;

	if (!strcmp(key, "ble_whitelist_app")) {
		rc = read_cb(cb_arg, get_app_whitelist_info(), sizeof(struct app_whitelist));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		return 0;
	}
	if (!strcmp(key, "dashboard_ts")) {
		if (get_dashboard_state()->dashboard_ts == NULL) {
			return 0;
		}
		rc = read_cb(cb_arg, get_dashboard_state()->dashboard_ts,
			     sizeof(struct dashboard_ts_context));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, *(int *)get_dashboard_state()->dashboard_ts);
		return 0;
	}
	if (!strcmp(key, "countdown_ts")) {
		if (get_dashboard_state()->countdown_ts == NULL) {
			return 0;
		}
		rc = read_cb(cb_arg, get_dashboard_state()->countdown_ts,
			     sizeof(struct countdown_ts_context));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, *(int *)get_dashboard_state()->countdown_ts);
		return 0;
	}
	if (!strcmp(key, "first_enter_flag")) {
		rc = read_cb(cb_arg, &get_dashboard_state()->first_enter_flag, sizeof(int));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, get_dashboard_state()->first_enter_flag);
		return 0;
	}
	if (!strcmp(key, "brightness_level")) {
		rc = read_cb(cb_arg, &get_dashboard_state()->panel.panel_brightness_level,
			     sizeof(uint8_t));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key,
			   get_dashboard_state()->panel.panel_brightness_level);
		return 0;
	}
	if (!strcmp(key, "3dof_enable")) {
		rc = read_cb(cb_arg, &get_dashboard_state()->imu.dof3_enable, sizeof(uint8_t));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, get_dashboard_state()->imu.dof3_enable);
		return 0;
	}
	if (!strcmp(key, "display_mode")) {
		rc = read_cb(cb_arg, &get_dashboard_state()->display_mode, sizeof(uint8_t));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, get_dashboard_state()->display_mode);
		return 0;
	}
	if (!strcmp(key, "runtime_mode")) {
		rc = read_cb(cb_arg, &get_dashboard_state()->work_mode, sizeof(uint8_t));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, get_dashboard_state()->work_mode);
		return 0;
	}
	if (!strcmp(key, "user_sched_info")) {
		if (get_dashboard_state()->user_sched_info == NULL) {
			return 0;
		}
		rc = read_cb(cb_arg, get_dashboard_state()->user_sched_info,
			     sizeof(struct user_schedule_context));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		return 0;
	}
	if (!strcmp(key, "wakeup_angle")) {
		rc = read_cb(cb_arg, &get_dashboard_state()->imu.level, sizeof(uint8_t));
		if (rc < 0) {
			G1_LOG_WRN("Failed to read %s, error: %d\n", key, rc);
			return rc;
		}
		G1_LOG_INF("key:%s, val=%d\n", key, get_dashboard_state()->imu.level);
		return 0;
	}
	return 0;
}

static struct settings_handler sett_conf = {
	.name = SETT_ROOT,
	.h_set = sett_handler,
};

static int local_store_load(void)
{
	return 0;
}

void sett_init(struct local_store *st)
{
	int err;

	err = settings_subsys_init();
	if (err) {
		G1_LOG_WRN("Failed to initialize settings subsystem, error: %d\n", err);
	} else {
		err = settings_register(&sett_conf);
		if (err) {
			G1_LOG_WRN("Cannot register settings handler %d\n", err);
		} else {
			G1_LOG_WRN("init ok\n");
		}
	}
	st->status = err ? err : 1;
	st->write = local_store_write;
	st->load = local_store_load;
	st->del = local_store_delete;
}

int flash_settings_read(uint32_t addr, void *buf, size_t len)
{
	int retry = 0;
	int ret;

	while (1) {
		if (!device_is_ready(FLASH_DEV)) {
			printk(" [%s] device not ready.\n", FLASH_DEV->name);
			ret = -1;
		} else {
			ret = get_dashboard_state()->flash_read(get_dashboard_state(), addr, buf,
								len);
			if (ret == 0) {
				break;
			}
			printk("Flash read failed! %d\n", ret);
		}
		if (retry == 10) {
			break;
		}
		k_sleep(K_MSEC(100));
		retry++;
		G1_LOG_ERR("flash read fail, retry again, retry time %d\n", retry);
	}
	return ret;
}

int flash_settings_write_and_verify(uint32_t addr, const void *data, size_t len)
{
	uint8_t *buf;
	int ret;

	buf = malloc(EXT_FLASH_SECTOR_SIZE);
	if (buf == NULL) {
		BLE_PRINTK("[%s-%d]error, have no memory ! \n", __func__, __LINE__);
		return -1;
	}
	if (!device_is_ready(FLASH_DEV)) {
		printk(" [%s] device not ready.\n", FLASH_DEV->name);
		ret = -1;
	} else {
		memset(buf, 0, EXT_FLASH_SECTOR_SIZE);
		ret = get_dashboard_state()->flash_read(get_dashboard_state(), addr, buf,
							EXT_FLASH_SECTOR_SIZE);
		if (ret) {
			printk("Flash read failed! %d\n", ret);
		} else if (memcmp(data, buf, len) == 0) {
			printk("write data and read data is same, exit\n");
		} else {
			ret = get_dashboard_state()->flash_erase(get_dashboard_state(), addr,
								 EXT_FLASH_SECTOR_SIZE);
			if (ret) {
				printk("Flash erase failed! %d\n", ret);
			} else {
				memset(buf, 0, EXT_FLASH_SECTOR_SIZE);
				memcpy(buf, data, len);
				ret = get_dashboard_state()->flash_write(
					get_dashboard_state(), addr, buf, EXT_FLASH_SECTOR_SIZE);
				if (ret) {
					printk("Flash write failed! %d addr %08X size %d\n", ret,
					       addr, len);
				} else {
					ret = get_dashboard_state()->flash_read(
						get_dashboard_state(), addr, buf,
						EXT_FLASH_SECTOR_SIZE);
					if (ret) {
						printk("Flash read failed! %d\n", ret);
					} else if (memcmp(data, buf, len) == 0) {
						printk("flash verify success!\n");
					} else {
						printk("flash verify failed\n");
						ret = 1;
					}
				}
			}
		}
	}
	free(buf);
	return ret;
}

int write_whitelist_to_flash(uint32_t addr, const uint8_t *data, uint32_t size)
{
	int ret = 0;
	uint8_t pages;
	int16_t last;
	uint8_t i;

	BLE_PRINTK("size is %d\n", size);
	pages = size / EXT_FLASH_SECTOR_SIZE + 1;
	last = size - (pages - 1) * EXT_FLASH_SECTOR_SIZE;
	for (i = 0; i < pages; i++) {
		ret += flash_settings_write_and_verify(
			addr + i * EXT_FLASH_SECTOR_SIZE, data + i * EXT_FLASH_SECTOR_SIZE,
			i < pages - 1 ? EXT_FLASH_SECTOR_SIZE : last);
	}
	return ret;
}

int write_font(uint32_t addr, const void *data, size_t len)
{
	uint8_t buf[EXT_FLASH_SECTOR_SIZE] = {0};
	int retry = 0;
	int ret;

	while (1) {
		if (!device_is_ready(FLASH_DEV)) {
			printk(" [%s] device not ready.\n", FLASH_DEV->name);
			ret = -1;
		} else {
			ret = get_dashboard_state()->flash_read(get_dashboard_state(), addr, buf,
								EXT_FLASH_SECTOR_SIZE);
			if (ret) {
				printk("Flash read font failed! %d\n", ret);
			} else if (memcmp(data, buf, len) == 0) {
				printk("write font data and read data is same, exit\n");
				return 0;
			} else {
				ret = get_dashboard_state()->flash_erase(
					get_dashboard_state(), addr, EXT_FLASH_SECTOR_SIZE);
				if (ret) {
					printk("Flash erase font failed! %d\n", ret);
				} else {
					ret = get_dashboard_state()->flash_write(
						get_dashboard_state(), addr, data, len);
					if (ret) {
						printk("Flash write font failed! %d\n", ret);
					} else {
						ret = get_dashboard_state()->flash_read(
							get_dashboard_state(), addr, buf,
							EXT_FLASH_SECTOR_SIZE);
						if (ret) {
							printk("Flash read font failed! %d\n", ret);
						} else if (memcmp(data, buf, len) == 0) {
							return 0;
						} else {
							printk("flash verify font failed\n");
							ret = 1;
						}
					}
				}
			}
		}
		retry++;
		if (retry == 10) {
			return ret;
		}
		k_sleep(K_MSEC(100));
	}
}

#define FLASH_CRC_CHUNK_LEN 1024

int calc_flash_crc(uint16_t *crc, int addr, uint32_t size)
{
	uint16_t pages;
	int pos;
	int len;
	int ret;
	int i;

	G1_LOG_INF("join in calc_flash_crc\n\n");
	if (crc == NULL || size == 0) {
		G1_LOG_INF("calc_flash_crc para is NULL\n\n");
		return -1;
	}
	pages = size / FLASH_CRC_CHUNK_LEN;
	if (size % FLASH_CRC_CHUNK_LEN) {
		pages++;
	}
	uint8_t buf[FLASH_CRC_CHUNK_LEN] = {0xff};

	if (!device_is_ready(FLASH_DEV)) {
		printk(" [%s] device not ready.\n", FLASH_DEV->name);
		return -1;
	}
	for (uint16_t page = 0; page < pages; page++) {
		pos = addr + page * FLASH_CRC_CHUNK_LEN;
		len = size + addr - pos;
		if (len >= FLASH_CRC_CHUNK_LEN) {
			len = FLASH_CRC_CHUNK_LEN;
		}
		ret = get_dashboard_state()->flash_read(get_dashboard_state(), pos, buf, len);
		if (ret) {
			printk("Flash read 0x%x failed: %d\n", pos, ret);
			return -1;
		}
		for (i = 0; i < len; i++) {
			G1_LOG_INF("0x%x \n", buf[i]);
		}
		G1_LOG_INF("\n\n");
		*crc = crc16_usb(*crc, buf, len);
	}
	if (watchdog_enabled && !fuel_gauge_stuck) {
		watchdog_feed();
	}
	return 0;
}
