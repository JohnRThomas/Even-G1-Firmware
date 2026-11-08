/*
 * Charging-case state: in the case, lid closed, charging, as detected over NFC.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "system/logging.h"
#include "services/power/charger_box.h"

static uint8_t nfc_box_status[3];
static uint8_t in_box_last;
static uint8_t lid_closed_last;
static uint8_t charging_last;

uint8_t get_box_status_by_type(uint8_t type)
{
	if (type > 2) {
		return 0xfe;
	}
	return nfc_box_status[type];
}

void set_box_status(uint8_t type, uint8_t status)
{
	if (type > 2) {
		return;
	}
	if (status) {
		nfc_box_status[type] = 1;
	} else {
		nfc_box_status[type] = 0;
	}
}

int is_in_box(void)
{
	uint8_t status = nfc_box_status[0];

	if (in_box_last != status) {
		if (BLE_DEBUG) {
			ble_printk("[csh_debug_box]isInbox is %d \n", status);
		} else {
			printk("[csh_debug_box]isInbox is %d \n", status);
		}
	}
	in_box_last = status;
	return status;
}

uint8_t is_box_lid_closed(void)
{
	uint8_t status = nfc_box_status[1];

	if (lid_closed_last != status) {
		printk("[csh_debug_box]isLidCLose is %d \n", status);
	}
	lid_closed_last = status;
	return status;
}

uint8_t is_box_charging(void)
{
	uint8_t status = nfc_box_status[2];

	if (charging_last != status) {
		printk("[csh_debug_box] isInCharge is %d \n", status);
	}
	charging_last = status;
	return status;
}

void init_box_status(void)
{
	nfc_box_status[0] = 0;
	nfc_box_status[1] = 0;
	nfc_box_status[2] = 0;
}
