/*
 * Work mode: which modules (IMU, touch, NFC, BLE, ESB, display system, ...) are
 * switched on in each operating mode, the DFU switch and ship mode entry.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include "system/work_mode.h"
#include "system/logging.h"
#include "system/power.h"
#include "main.h"
#include "comm/ble/ble.h"
#include "comm/esb/esb.h"
#include "comm/ipc/ipc_host.h"
#include "drivers/gpio/board_gpio.h"
#include "services/imu/imu_fusion.h"
#include "services/production_test/production_test.h"
#include "system/system_state.h"

#define DFU_FLASH DEVICE_DT_GET(DT_NODELABEL(mx25r64))

enum work_module {
	WORK_MODULE_TOUCH = 0x001,
	WORK_MODULE_DMIC = 0x002,
	WORK_MODULE_0008 = 0x008,
	WORK_MODULE_EXT_FLASH = 0x010,
	WORK_MODULE_IMU = 0x020,
	WORK_MODULE_ESB = 0x040,
	WORK_MODULE_BLE = 0x080,
	WORK_MODULE_SYSTEM = 0x100,
	WORK_MODULE_NFC = 0x400,
	WORK_MODULE_SHIPMODE = 0x800,
};

uint8_t runtime_mode; /* settings key "runtime_mode": 1 = boot into DFU mode */
static uint8_t work_mode_busy;
void switch_to_dfu_mode(struct k_sem *sem)
{
	int ret;

	runtime_mode = 1;
	if (k_is_in_isr()) {
		k_sem_give(sem);
		return;
	}
	ret = GLOBAL_STATE->store.write("runtime_mode", &runtime_mode, sizeof(uint8_t));
	if (ret != 0) {
		G1_LOG(0, "failed to switch to ble mode [%d]\n", ret);
		return;
	}
	G1_LOG(0, "sys reboot because system reset %d, %d\n", 0, runtime_mode);
	system_reboot_delayed();
}

int erase_dfu_flash(void)
{
	const struct device *flash = DFU_FLASH;

	if (!device_is_ready(flash)) {
		printk(" [%s] device not ready.\n", flash->name);
		return -1;
	}
	flash_erase(flash, 0, 0x130000);
	printk("erase_dfu_flash done.\n");
	return 0;
}

static void imu_module_set_mode(int enable)
{
	int mode = 0;

	if (enable == 1) {
		mode = 1;
	} else if (enable == 2) {
		mode = 2;
	}
	update_imu_mode(&GLOBAL_STATE->imu, mode);
}

static void update_work_module(int enable, int module)
{
	struct GlassesState *gs = GLOBAL_STATE;

	if (enable) {
		gs->module_mask |= module;
	} else {
		gs->module_mask &= ~module;
	}
	if (!gs->work_mode) {
		return;
	}
	switch (module) {
	case WORK_MODULE_IMU:
		if (gs->is_master == true) {
			imu_module_set_mode(enable);
		} else {
			k_msleep(50);
		}
		break;
	case WORK_MODULE_TOUCH:
		if (enable) {
			touch_power_on();
		} else {
			touch_power_off();
		}
		break;
	case WORK_MODULE_NFC:
		if (enable) {
			nfc_lpd_off();
		} else {
			nfc_lpd_on();
		}
		break;
	case WORK_MODULE_SYSTEM:
		if (get_pt_test_mode() == 0) {
			if (enable) {
				global_system_resume(GLOBAL_STATE);
			} else {
				global_system_suspend(GLOBAL_STATE);
			}
		}
		break;
	case WORK_MODULE_BLE:
		if (enable) {
			bt_start();
		} else {
			stop_ble_advertising();
		}
		break;
	case WORK_MODULE_ESB:
		switch (enable) {
		case 2:
			set_band_for_ESB(0x0a);
			break;
		case 1:
			set_band_for_ESB(0x0b);
			break;
		default:
			set_band_for_ESB(0x0c);
			break;
		}
		break;
	case WORK_MODULE_SHIPMODE:
		if (!enable) {
			set_shipmode(1);
		}
		break;
	default:
		break;
	}
}

int get_work_mode(void)
{
	return GLOBAL_STATE->work_mode;
}

int change_work_mode(int mode)
{
	int retry = 5000;

	while (work_mode_busy) {
		if (--retry == 0) {
			break;
		}
		k_msleep(1);
	}
	work_mode_busy = 1;
	if (GLOBAL_STATE->work_mode != mode || GLOBAL_STATE->suspend_state == 5) {
		GLOBAL_STATE->work_mode = mode;
		G1_LOG(0, "change_work_mode to %d\n", (uint8_t)mode);

		switch (GLOBAL_STATE->work_mode) {
		case 0:
			update_work_module(1, WORK_MODULE_EXT_FLASH);
			update_work_module(1, WORK_MODULE_SYSTEM);
			update_work_module(1, WORK_MODULE_IMU);
			update_work_module(1, WORK_MODULE_BLE);
			update_work_module(1, WORK_MODULE_ESB);
			update_work_module(1, WORK_MODULE_0008);
			update_work_module(1, WORK_MODULE_DMIC);
			update_work_module(1, WORK_MODULE_TOUCH);
			update_work_module(1, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			break;
		case 1:
			update_work_module(1, WORK_MODULE_EXT_FLASH);
			update_work_module(0, WORK_MODULE_SYSTEM);
			update_work_module(0, WORK_MODULE_IMU);
			update_work_module(1, WORK_MODULE_BLE);
			update_work_module(0, WORK_MODULE_ESB);
			update_work_module(0, WORK_MODULE_0008);
			update_work_module(0, WORK_MODULE_DMIC);
			update_work_module(0, WORK_MODULE_TOUCH);
			update_work_module(1, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			{
				uint8_t msg[64] = {0};

				msg[0] = 1;
				msg[1] = 8;
				global_ipc_service_send(msg, 4);
			}
			break;
		case 2:
			update_work_module(1, WORK_MODULE_EXT_FLASH);
			update_work_module(1, WORK_MODULE_SYSTEM);
			update_work_module(2, WORK_MODULE_IMU);
			update_work_module(1, WORK_MODULE_BLE);
			update_work_module(1, WORK_MODULE_ESB);
			update_work_module(0, WORK_MODULE_0008);
			update_work_module(1, WORK_MODULE_DMIC);
			update_work_module(1, WORK_MODULE_TOUCH);
			update_work_module(1, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			memset(GLOBAL_STATE->ui.buf, 0, sizeof(GLOBAL_STATE->ui.buf));
			break;
		case 3:
			update_work_module(0, WORK_MODULE_SYSTEM);
			update_work_module(0, WORK_MODULE_EXT_FLASH);
			update_work_module(1, WORK_MODULE_IMU);
			update_work_module(1, WORK_MODULE_BLE);
			update_work_module(1, WORK_MODULE_ESB);
			update_work_module(0, WORK_MODULE_0008);
			update_work_module(1, WORK_MODULE_DMIC);
			update_work_module(1, WORK_MODULE_TOUCH);
			update_work_module(1, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			break;
		case 4:
		case 5:
		case 6:
			update_work_module(0, WORK_MODULE_EXT_FLASH);
			update_work_module(0, WORK_MODULE_SYSTEM);
			update_work_module(0, WORK_MODULE_IMU);
			update_work_module(1, WORK_MODULE_BLE);
			update_work_module(1, WORK_MODULE_ESB);
			update_work_module(0, WORK_MODULE_0008);
			update_work_module(0, WORK_MODULE_DMIC);
			update_work_module(0, WORK_MODULE_TOUCH);
			update_work_module(1, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			break;
		case 8:
			update_work_module(0, WORK_MODULE_SYSTEM);
			update_work_module(0, WORK_MODULE_EXT_FLASH);
			update_work_module(0, WORK_MODULE_IMU);
			update_work_module(0, WORK_MODULE_BLE);
			update_work_module(0, WORK_MODULE_ESB);
			update_work_module(0, WORK_MODULE_0008);
			update_work_module(0, WORK_MODULE_DMIC);
			update_work_module(0, WORK_MODULE_TOUCH);
			update_work_module(0, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			break;
		case 9:
			update_work_module(0, WORK_MODULE_SYSTEM);
			update_work_module(1, WORK_MODULE_EXT_FLASH);
			update_work_module(1, WORK_MODULE_IMU);
			update_work_module(0, WORK_MODULE_BLE);
			update_work_module(0, WORK_MODULE_ESB);
			update_work_module(1, WORK_MODULE_0008);
			update_work_module(1, WORK_MODULE_DMIC);
			update_work_module(1, WORK_MODULE_TOUCH);
			update_work_module(1, WORK_MODULE_NFC);
			update_work_module(1, WORK_MODULE_SHIPMODE);
			break;
		case 7:
			set_shipmode(1);
			break;
		default:
			break;
		}
	}
	work_mode_busy = 0;
	return 0;
}
