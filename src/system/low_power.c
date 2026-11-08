/*
 * Low-power mode in the charging case: with the lid closed and the battery full
 * the network core is switched off and the glasses idle until the case is opened.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <nrfx.h>
#include "system/low_power.h"
#include "system/logging.h"
#include "system/monitor.h"
#include "system/watchdog.h"
#include "system/work_mode.h"
#include "services/nfc_wlc/wlc.h"
#include "services/power/fuel_gauge.h"
#include "services/power/charger_box.h"

#define CHARGER_DEV DEVICE_DT_GET(DT_NODELABEL(pmic_charger))

uint8_t can_enter_low_power;
static uint8_t low_power_entered;
static uint64_t last_activity_ms; /* set by update_activity_time, 5 min low-power window */
void try_enter_low_power_mode(void)
{
	uint16_t wait = 0;
	uint16_t i;
	uint8_t n;

	if (chargerGetVdcRect() >= 5000 || get_dashboard_state()->battery_info[0] < 98 ||
	    !can_enter_low_power) {
		return;
	}
	while (1) {
		G1_LOG_ERR(
			"try to sleep chargerGetVdcRect() %d GLOBAL->battery_info[0] %d can_enter_low_power %d\n",
			chargerGetVdcRect(), get_dashboard_state()->battery_info[0],
			can_enter_low_power);
		for (i = 0; i < 10; i++) {
			if (watchdog_enabled && !fuel_gauge_stuck) {
				watchdog_feed();
			}
			k_msleep(1000);
			nfc_wlc_process();
			fuel_gauge_update(CHARGER_DEV);
			if (chargerGetVdcRect() < 1000) {
				break;
			}
		}
		if (!can_enter_low_power) {
			if (is_box_lid_closed() == 1) {
				G1_LOG_ERR(
					"glass box is close, wait enter low power mode again.\n");
				continue;
			}
			G1_LOG_ERR("force reboot becase can not enter low power mode %d\n",
				   chargerGetVdcRect());
			k_msleep(500);
			sys_reboot(SYS_REBOOT_COLD);
		}
		if (chargerGetVdcRect() > 1000 || is_box_lid_closed() != 1) {
			if (i != 10) {
				G1_LOG_ERR(
					"force exit low power mode becase can not enter low power mode %d\n",
					chargerGetVdcRect());
				change_work_mode(9);
			}
			continue;
		}
		can_enter_low_power = 2;
		while (!low_power_entered && k_uptime_get() - last_activity_ms < 300000) {
			if (watchdog_enabled && !fuel_gauge_stuck) {
				watchdog_feed();
			}
			if (!can_enter_low_power) {
				return;
			}
			k_msleep(1000);
		}
		change_work_mode(8);
		NRF_RESET_S->NETWORK.FORCEOFF = RESET_NETWORK_FORCEOFF_FORCEOFF_Hold;
		low_power_entered = 1;
		G1_LOG_ERR("enter low power mode\n");
		while (get_dashboard_state()->battery_info[0] > 94 && can_enter_low_power) {
			if (wait % 20 == 0 && watchdog_enabled && !fuel_gauge_stuck) {
				watchdog_feed();
			}
			wait++;
			k_msleep(50);
			if (wait >= 1200) {
				fuel_gauge_update(CHARGER_DEV);
				wait = 0;
			}
		}
		change_work_mode(9);
		G1_LOG_ERR(
			"wakeup chargerGetVdcRect() %d GLOBAL->battery_info[0] %d can_enter_low_power %d\n",
			chargerGetVdcRect(), get_dashboard_state()->battery_info[0],
			can_enter_low_power);
		for (n = 0; n < 40 && !can_enter_low_power; n++) {
			if (watchdog_enabled && n % 10 == 0 && !fuel_gauge_stuck) {
				watchdog_feed();
			}
			k_msleep(100);
			nfc_wlc_process();
			fuel_gauge_update(CHARGER_DEV);
		}
		if (is_box_lid_closed() != 1 && chargerGetVdcRect() <= 1000) {
			G1_LOG_ERR("force reboot becase can not get vac %d\n", chargerGetVdcRect());
			k_msleep(500);
			sys_reboot(SYS_REBOOT_COLD);
		}
		k_msleep(6000);
	}
}

void update_activity_time(void)
{
	last_activity_ms = k_uptime_get();
}
