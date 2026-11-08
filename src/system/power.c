/*
 * Power policy above the PMIC driver: ship mode entry, the light sensor that
 * shares the panel's 1.8 V rail, and the hooks around external flash accesses.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "system/power.h"
#include "drivers/pmic/npm1300.h"
#include "drivers/ambient/opt3007.h"

int set_shipmode(int enable)
{
	power_down_panel();
	power_down_imu_and_mic();
	k_msleep(2000);
	npm1300_enter_ship_mode();
	return 0;
}

/* empty hooks called around external flash accesses (ext_flash_read/write/erase) */
void ext_flash_power_up(void)
{
}

void ext_flash_power_down(void)
{
}

int enable_power_for_panel(void)
{
	if (npm1300_ldsw1_set(true) < 0) {
		return 0;
	}
	opt3007_chip_init(NULL);
	return 0;
}

int disable_power_for_panel(void)
{
	if (npm1300_ldsw1_set(false) < 0) {
		return 0;
	}
	opt3007_chip_deinit();
	return 0;
}
