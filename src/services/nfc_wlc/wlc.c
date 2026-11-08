/*
 * NFC wireless charging (NFC Forum WLC listener) over the ST25DV tag: field
 * supervision, charge supervision and the WLC listener state machine that
 * publishes the WLCCAP / WLCSTAI / WLCFOD message on the tag.
 *
 * The field voltage comes from drivers/nfc/field_adc.c, the NDEF encoding is
 * in ndef*.c, the type 5 tag layout in tagtype5.c.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/sys/printk.h>

#include "system/logging.h"

#include "drivers/nfc/field_adc.h"
#include "drivers/nfc/st25dv.h"
#include "services/nfc_wlc/ndef.h"
#include "services/nfc_wlc/ndef_types.h"
#include "services/nfc_wlc/ndef_wlc.h"
#include "services/nfc_wlc/tagtype5.h"
#include "services/nfc_wlc/wlc.h"
#include "services/production_test/production_test.h"
#include "system/glasses_state.h"
#include "services/production_test/pt_aging.h"
#include "drivers/nfc/eeprom_st25dv.h"

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

/* WLC listener state */
struct wlc_listener {
	uint8_t active;
	uint8_t fod_req;
	uint8_t charging;
	uint8_t field_lost;
	uint8_t f04;
	uint8_t state;  /* state machine, 1 = (re)initialise */
	uint8_t f06;
	uint8_t cap_info[10]; /* [0] != 0 while charging */
	uint8_t stai_info[11]; /* [0] present, [2] battery */
	uint8_t fod_info[3];
	uint8_t f1f[6];
	uint8_t f25;
	uint8_t f26;
	uint8_t f27;
	uint32_t f28;
	uint8_t f2c;
	uint8_t f2d;
	uint8_t f2e[2];
	uint8_t f30[2];
	uint8_t f32[2];
	uint8_t battery;
	uint8_t f35[3];
	uint8_t f38;
	uint8_t f39;
	uint8_t f3a;
	uint8_t f3b;
};

/* NFC field / charge supervision */
struct nfc_charge {
	uint8_t f00;
	uint8_t f01;
	uint8_t state;  /* 1 = charging, 2 = no field, 3 = updating, 4 = battery full */
	uint8_t f03[5];
	uint32_t field_mv; /* nfc_field_adc_read_mv() * 10 */
};

static struct wlc_listener wlc_listener = {
	.state = 1,
};
static uint8_t wlc_info_sent = 1;

static struct nfc_charge nfc_charge;
static uint32_t nfc_tick_start;
static uint32_t nfc_field_mv;  /* last NFC field voltage (mV * 10) */
static uint32_t nfc_field_tick;
static float wlc_wait_ms;
static uint32_t nfc_charge_tick;
static int wlc_tag_configured;

static uint8_t nfc_tag_type_ok;  /* ST25DV of the expected type (UID byte 6 is 0x50/0x51) */
static uint8_t nfc_field_on;
static uint8_t wlc_field_present;
static uint8_t nfc_low_field;
static uint16_t wlc_last_battery;

/* ------------------------------------------------------------------ */
/* listener timer                                                      */
/* ------------------------------------------------------------------ */

static uint32_t nfc_tick_elapsed(void)
{
	return (uint32_t)k_uptime_ticks() - nfc_tick_start;
}

static void nfc_tick_reset(void)
{
	nfc_tick_start = k_uptime_ticks();
}

/* ------------------------------------------------------------------ */
/* NFC field supervision                                               */
/* ------------------------------------------------------------------ */

static int nfc_read_field_mv(uint32_t *field_mv)
{
	*field_mv = nfc_field_adc_read_mv() * 10;
	return 1;
}

static void nfc_field_voltage_update(void)
{
	nfc_read_field_mv(&nfc_charge.field_mv);
	nfc_field_mv = nfc_charge.field_mv;
}

uint32_t nfc_get_field_mv(void)
{
	return nfc_field_mv;
}

uint32_t chargerGetVdcRect(void)
{
	nfc_field_voltage_update();
	return nfc_field_mv;
}

static bool nfc_field_detected(void)
{
	nfc_field_voltage_update();
	return nfc_field_mv > 5100;
}

static int nfc_field_check(void)
{
	int ret;

	if (nfc_field_detected()) {
		nfc_field_tick = k_uptime_ticks();
		ret = 1;
	} else {
		if ((int32_t)((uint32_t)k_uptime_ticks() - nfc_field_tick) < 0) {
			k_uptime_ticks();
		}
		ret = 0;
	}

	return ret;
}

/* ------------------------------------------------------------------ */
/* charge supervision                                                  */
/* ------------------------------------------------------------------ */

static int nfc_charge_status(void)
{
	if (nfc_field_mv <= 5100) {
		return 2;
	}

	return get_dashboard_state()->battery_left >= 100 ? 1 : 0;
}

static void nfc_charge_update_state(struct nfc_charge *chg)
{
	int status = nfc_charge_status();

	if (status == 1) {
		chg->state = 4;
	} else if (status == 0) {
		chg->state = 1;
	} else {
		chg->state = 2;
	}
}

static void nfc_charge_update(struct nfc_charge *chg, struct wlc_listener *wlc)
{
	chg->state = 3;
	nfc_charge_update_state(chg);

	switch (chg->state) {
	case 1:
		wlc->cap_info[5] = 9;
		wlc->field_lost = 0;
		if (chg->field_mv < 500) {
			if (!nfc_low_field) {
				nfc_low_field = 1;
			}
			return;
		}
		nfc_low_field = 0;
		break;
	case 2:
		if (k_uptime_ticks() - nfc_charge_tick <= 3000) {
			return;
		}
		if (wlc->state == 10) {
			if (!wlc->active) {
				wlc->cap_info[5] = 6;
			}
			wlc->fod_req = 1;
		}
		wlc->field_lost = 1;
		nfc_charge_tick = k_uptime_ticks();
		return;
	case 4:
		if (wlc->state == 10) {
			if (!wlc->active) {
				wlc->cap_info[5] = 6;
			}
			wlc->fod_req = 1;
		}
		if (!wlc->charging) {
			wlc->charging = 1;
		}
		break;
	default:
		break;
	}

	nfc_charge_tick = k_uptime_ticks();
}

static void nfc_charge_refresh(struct nfc_charge *chg, struct wlc_listener *wlc)
{
	nfc_read_field_mv(&chg->field_mv);
	nfc_field_mv = chg->field_mv;

	wlc->stai_info[4] = (get_dashboard_state()->battery_voltage + 200u) >> 8;
	wlc->stai_info[2] = get_dashboard_state()->battery_left;
	wlc->battery = get_dashboard_state()->battery_left;
	(void)get_dashboard_state();
	wlc->charging = 0;

	nfc_charge_update(chg, wlc);
}

/* ------------------------------------------------------------------ */
/* tag initialisation                                                  */
/* ------------------------------------------------------------------ */

int nfc_init(void)
{
	uint8_t uid[8] = {0};
	uint8_t ic_rev = 0;
	uint8_t ic_ref;
	const struct eeprom_driver_api *api;

	global_st25dv_context_ptr = eeprom_st25dv_init();
	if (!global_st25dv_context_ptr || !global_st25dv_context_ptr->config ||
	    !global_st25dv_context_ptr->data || !global_st25dv_context_ptr->api) {
		return 0;
	}

	k_msleep(20);
	api = global_st25dv_context_ptr->api;
	api->size(global_st25dv_context_ptr);

	ST25DV_GetUID(global_st25dv_context_ptr, uid);
	printk("UUID = %02X %02X %02X %02X %02X %02X %02X %02X.\n", uid[0], uid[1], uid[2], uid[3],
	       uid[4], uid[5], uid[6], uid[7]);
	if (uid[5] == 0x50 || uid[5] == 0x51) {
		nfc_tag_type_ok = 1;
	}

	st25dv_read_ic_rev(global_st25dv_context_ptr, &ic_rev);
	printk("REV_IC = %02X.\n", ic_rev);
	st25dv_read_id(global_st25dv_context_ptr, &ic_ref);
	printk("REF_IC = %02X.\n", ic_ref);

	nfc_field_adc_init();

	return 0;
}

uint8_t nfc_is_tag_type_ok(void)
{
	return nfc_tag_type_ok;
}

/* ------------------------------------------------------------------ */
/* WLC listener NDEF message (WLCCAP [+ WLCSTAI] [+ WLCFOD])           */
/* ------------------------------------------------------------------ */

static int wlc_write_ndef_message(const uint8_t *cap_info, const uint8_t *stai_info,
				  const uint8_t *fod_info)
{
	ndefBuffer bufPayload;
	ndefTypeRtdWlcStatusInfo wlcStatusInfo;
	ndefTypeRtdWlcCapability wlcCapability;
	ndefRecord recordCap;
	ndefRecord recordFod;
	ndefRecord recordStai;
	uint8_t buf[32];
	ndefType typeCap;
	ndefType typeStai;
	ndefRecord records[4];
	ndefMessage message;
	ReturnCode err;
	int count;
	int i;

	wlcCapability.capWtInt = cap_info[4];
	wlcCapability.ndefRdWt = cap_info[6];
	wlcCapability.ndefWriteToInt = cap_info[7];
	wlcCapability.ndefWriteWtInt = cap_info[8];
	wlcCapability.wlcConfigModeReq = cap_info[0];
	wlcCapability.wlcConfigNegoWait = cap_info[2];
	wlcCapability.wlcConfigRdConf = cap_info[3];
	wlcCapability.wlcConfigWaitTimeRetry = cap_info[1];
	wlcCapability.wlcProtocolVersion = 0x20;
	wlcCapability.capWtIntRfu = cap_info[9];
	err = ndefRtdWlcCapabilityInit(&typeCap, &wlcCapability) |
	      ndefTypeToRecord(&typeCap, &recordCap);
	records[0] = recordCap;

	if (!stai_info[0]) {
		count = 1;
	} else {
		wlcStatusInfo.controlByte1 = stai_info[1];
		wlcStatusInfo.batteryLevel = stai_info[2];
		wlcStatusInfo.receivePower = stai_info[3];
		wlcStatusInfo.receiveVoltage = stai_info[4];
		wlcStatusInfo.receiveCurrent = stai_info[5];
		wlcStatusInfo.temperatureBattery = stai_info[6];
		wlcStatusInfo.temperatureWlcl = stai_info[7];
		wlcStatusInfo.controlByte2 = stai_info[8];
		count = 2;
		err |= ndefRtdWlcStatusInfoInit(&typeStai, &wlcStatusInfo) |
		       ndefTypeToRecord(&typeStai, &recordStai);
		records[1] = recordStai;
	}

	if (fod_info[0]) {
		ndefConstBuffer bufRecord;

		/* raw WLCFOD record (not in the ST library): ME|SR|TNF=1, type "WLCFOD", 2 bytes
		 * payload */
		buf[0] = 0x51;
		buf[1] = 0x06;
		buf[2] = 0x02;
		buf[3] = 'W';
		buf[4] = 'L';
		buf[5] = 'C';
		buf[6] = 'F';
		buf[7] = 'O';
		buf[8] = 'D';
		buf[9] = fod_info[1];
		buf[10] = fod_info[2];
		bufRecord.buffer = buf;
		bufRecord.length = 11;
		err = ndefRecordDecode(&bufRecord, &recordFod);
		records[count] = recordFod;
		count++;
	}

	ndefMessageInit(&message);
	for (i = 0; i < count; i++) {
		err |= ndefMessageAppend(&message, &records[i]);
	}

	bufPayload.buffer = buf;
	bufPayload.length = sizeof(buf);
	err |= ndefMessageEncode(&message, &bufPayload);

	return (err | (uint16_t)nfc_write_ndef(bufPayload.buffer, bufPayload.length)) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* WLC listener state machine                                          */
/* ------------------------------------------------------------------ */

static void wlc_reset_info(struct wlc_listener *wlc)
{
	wlc->cap_info[4] = 9;
	wlc->cap_info[5] = 9;
	wlc->f30[0] = 9;
	wlc->f30[1] = 9;
}

static void wlc_update_field_state(struct wlc_listener *wlc, uint8_t *present)
{
	int on = nfc_field_check();

	*present = on != 0;
	if (!on) {
		if (nfc_field_on) {
			nfc_field_on = 0;
			if (!wlc->charging) {
				wlc->state = 1;
			}
		}
	} else if (!nfc_field_on) {
		nfc_field_on = 1;
	}
}

static inline bool wlc_battery_changed(struct wlc_listener *wlc)
{
	int diff = wlc->stai_info[2] - wlc_last_battery;

	if (diff < 0) {
		diff = wlc_last_battery - wlc->stai_info[2];
	}
	if (diff > 20) {
		wlc_last_battery = wlc->stai_info[2];
		return true;
	}
	return false;
}

static void wlc_listener_process(struct wlc_listener *wlc)
{
	wlc_update_field_state(wlc, &wlc_field_present);
	wlc_field_present = 1;

	switch (wlc->state) {
	case 1:
		wlc_reset_info(wlc);
		wlc->state = 2;
		wlc->charging = 0;
		return;
	case 2:
		k_msleep(50);
		wlc->state = 3;
		return;
	case 3:
		if (wlc->charging) {
			wlc->state = 12;
			return;
		}
		if (!wlc->cap_info[0]) {
			if (wlc_info_sent || wlc->cap_info[4] != wlc->cap_info[5] ||
			    wlc_battery_changed(wlc)) {
				wlc->cap_info[4] = wlc->cap_info[5];
				wlc_write_ndef_message(wlc->cap_info, wlc->stai_info,
						       wlc->fod_info);
				wlc_info_sent = 0;
			}
			wlc->state = 4;
		}
		if (wlc->fod_req) {
			wlc->fod_req = 0;
		}
		wlc_wait_ms = 150.0f;
		nfc_tick_reset();
		return;
	case 4:
		if (!((float)nfc_tick_elapsed() > wlc_wait_ms)) {
			return;
		}
		if (!wlc->cap_info[0]) {
			wlc->state = 9;
		} else {
			wlc->state = 5;
		}
		return;
	case 5:
	case 6:
	case 7:
	case 9:
		if (!wlc->cap_info[0]) {
			wlc_wait_ms = pow(2.0, wlc->cap_info[4] + 3) - 50.0;
		}
		nfc_tick_reset();
		wlc->state = 10;
		return;
	case 10:
		if (!wlc->fod_req && !((float)nfc_tick_elapsed() >= wlc_wait_ms)) {
			return;
		}
		wlc->state = 11;
		return;
	case 11:
		if (wlc->fod_req) {
			if (wlc->charging) {
				if (!wlc->cap_info[0]) {
					wlc->state = 12;
				}
			} else if (!wlc->cap_info[0]) {
				wlc->state = 2;
			}
		} else if (!wlc->cap_info[0]) {
			wlc->state = 3;
		}
		return;
	case 12:
		wlc->cap_info[0] = 2;
		wlc->cap_info[4] = 0x13;
		wlc_write_ndef_message(wlc->cap_info, wlc->stai_info, wlc->fod_info);
		wlc_info_sent = 1;
		wlc->fod_req = 0;
		nfc_tick_reset();
		wlc->state = 14;
		return;
	case 14:
		nfc_tick_reset();
		return;
	default:
		return;
	}
}

int nfc_wlc_process(void)
{
	uint8_t eh_ctrl = 0;

	enable_charger_retry();

	if (!wlc_tag_configured) {
		if (st25dv_i2c_session_open(1) < 0) {
			return 0;
		}
		if (st25dv_read_eh_ctrl_dyn(global_st25dv_context_ptr, &eh_ctrl) == 0) {
			eh_ctrl &= ~BIT(0);
			st25dv_write_eh_ctrl_dyn(global_st25dv_context_ptr, eh_ctrl);
		}
		st25dv_gpo_setup(0xff);
		if (st25dv_i2c_session_open(0) < 0) {
			return 0;
		}
		wlc_tag_configured = 1;
	}

	if (wlc_listener.state == 1) {
		wlc_listener.active = 1;
		wlc_listener.fod_req = 0;
		wlc_listener.charging = 0;
		wlc_listener.field_lost = 0;
		wlc_listener.f04 = 1;
		wlc_listener.f06 = 0;
		wlc_listener.cap_info[0] = 0;
		wlc_listener.cap_info[1] = 0;
		wlc_listener.cap_info[2] = 0;
		wlc_listener.cap_info[3] = 1;
		wlc_listener.cap_info[4] = 9;
		wlc_listener.cap_info[5] = 9;
		wlc_listener.cap_info[6] = 0x1e;
		wlc_listener.cap_info[7] = 3;
		wlc_listener.cap_info[8] = 0xa;
		wlc_listener.cap_info[9] = 0;
		wlc_listener.stai_info[0] = 1;
		wlc_listener.stai_info[1] = 5;
		wlc_listener.stai_info[2] = 0;
		wlc_listener.stai_info[3] = 0;
		wlc_listener.stai_info[4] = 0;
		wlc_listener.stai_info[5] = 0;
		wlc_listener.stai_info[6] = 0;
		wlc_listener.stai_info[7] = 0;
		wlc_listener.stai_info[8] = 0;
		wlc_listener.fod_info[0] = 0;
		wlc_listener.fod_info[1] = 2;
		wlc_listener.fod_info[2] = 0;
		wlc_listener.f25 = 0;
		wlc_listener.f27 = 0;
		wlc_listener.f28 = 0;
		wlc_listener.f2c = 0;
		wlc_listener.f2e[0] = 1;
		wlc_listener.f2e[1] = 1;
		wlc_listener.f30[0] = 0xa;
		wlc_listener.f30[1] = 0xa;
		wlc_listener.f32[0] = 0;
		wlc_listener.f32[1] = 0;
		wlc_listener.battery = 0;
		wlc_listener.f35[0] = 0;
		wlc_listener.f35[1] = 0;
		wlc_listener.f35[2] = 0xf;
		wlc_listener.f38 = 0;
		wlc_listener.f3a = 0;
	}

	nfc_charge_refresh(&nfc_charge, &wlc_listener);
	get_pt_test_mode();
	wlc_listener_process(&wlc_listener);

	return 0;
}
