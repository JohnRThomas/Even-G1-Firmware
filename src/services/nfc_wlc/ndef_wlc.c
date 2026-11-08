/*
 * WLCCAP and WLCSTAI record types, from ST's RFAL NDEF library
 * (ndef_type_wlc.c): payload length and payload item encoders, the type
 * initialisers and the type-to-record conversions.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/nfc_wlc/ndef.h"
#include "services/nfc_wlc/ndef_wlc.h"
#include "services/nfc_wlc/ndef_types.h"

/* RTD WLC type strings */
static const uint8_t ndefRtdTypeWlcCapability[] = "WLCCAP";
static const uint8_t ndefRtdTypeWlcStatusInfo[] = "WLCSTAI";

static ndefConstBuffer8 bufTypeRtdWlcCapability = {ndefRtdTypeWlcCapability,
						   sizeof(ndefRtdTypeWlcCapability) - 1U};
static ndefConstBuffer8 bufTypeRtdWlcStatusInfo = {ndefRtdTypeWlcStatusInfo,
						   sizeof(ndefRtdTypeWlcStatusInfo) - 1U};

/* payload item iterators */
static uint32_t wlcCapabilityItem;
static uint32_t wlcStatusInfoItem;
static uint8_t wlcCapabilityTemp;

#define NDEF_TYPE_RTD_WLC_PAYLOAD_LENGTH 6U

#define NDEF_WLC_CAPABILITY_CONFIG_MODE_REQ_SHIFT        6U
#define NDEF_WLC_CAPABILITY_CONFIG_MODE_REQ_MASK         3U
#define NDEF_WLC_CAPABILITY_CONFIG_WAIT_TIME_RETRY_SHIFT 2U
#define NDEF_WLC_CAPABILITY_CONFIG_WAIT_TIME_RETRY_MASK  0xFU
#define NDEF_WLC_CAPABILITY_CONFIG_NEGO_WAIT_SHIFT       1U
#define NDEF_WLC_CAPABILITY_CONFIG_NEGO_WAIT_MASK        1U
#define NDEF_WLC_CAPABILITY_CONFIG_RD_CONF_SHIFT         0U
#define NDEF_WLC_CAPABILITY_CONFIG_RD_CONF_MASK          1U
#define NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_RFU_SHIFT  5U
#define NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_RFU_MASK   0x7U
#define NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_SHIFT      0U
#define NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_MASK       0x1FU

/* ------------------------------------------------------------------ */
/* WLC Capability (WLCCAP)                                             */
/* ------------------------------------------------------------------ */

static uint32_t ndefRtdWlcCapabilityGetPayloadLength(const ndefType *type)
{
	if ((type == NULL) || (type->id != NDEF_TYPE_ID_RTD_WLCCAP)) {
		return 0;
	}

	return NDEF_TYPE_RTD_WLC_PAYLOAD_LENGTH;
}

static const uint8_t *ndefRtdWlcCapabilityGetPayloadItem(const ndefType *type,
							 ndefConstBuffer *bufItem, bool begin)
{
	const ndefTypeRtdWlcCapability *ndefData;

	if ((type == NULL) || (type->id != NDEF_TYPE_ID_RTD_WLCCAP) || (bufItem == NULL)) {
		return NULL;
	}

	ndefData = &type->data.wlcCapability;

	if (begin == true) {
		wlcCapabilityItem = 0;
	}

	/* Prepare bufItem->length: Each field is 1 byte, except when complete */
	bufItem->length = sizeof(uint8_t);

	switch (wlcCapabilityItem) {
	case 0: /* WLC Protocol Version */
		bufItem->buffer = &ndefData->wlcProtocolVersion;
		break;
	case 1: { /* WLC Config */
		wlcCapabilityTemp =
			((ndefData->wlcConfigModeReq & NDEF_WLC_CAPABILITY_CONFIG_MODE_REQ_MASK)
			 << NDEF_WLC_CAPABILITY_CONFIG_MODE_REQ_SHIFT) |
			((ndefData->wlcConfigWaitTimeRetry &
			  NDEF_WLC_CAPABILITY_CONFIG_WAIT_TIME_RETRY_MASK)
			 << NDEF_WLC_CAPABILITY_CONFIG_WAIT_TIME_RETRY_SHIFT) |
			((ndefData->wlcConfigNegoWait & NDEF_WLC_CAPABILITY_CONFIG_NEGO_WAIT_MASK)
			 << NDEF_WLC_CAPABILITY_CONFIG_NEGO_WAIT_SHIFT) |
			((ndefData->wlcConfigRdConf & NDEF_WLC_CAPABILITY_CONFIG_RD_CONF_MASK)
			 << NDEF_WLC_CAPABILITY_CONFIG_RD_CONF_SHIFT);

		bufItem->buffer = &wlcCapabilityTemp;
		break;
	}
	case 2: /* Cap Wt Int */
		wlcCapabilityTemp =
			((ndefData->capWtIntRfu & NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_RFU_MASK)
			 << NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_RFU_SHIFT) |
			((ndefData->capWtInt & NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_MASK)
			 << NDEF_WLC_CAPABILITY_CONFIG_CAP_WT_INT_SHIFT);

		bufItem->buffer = &wlcCapabilityTemp;
		break;
	case 3: /* NDEF Rd Wt */
		bufItem->buffer = &ndefData->ndefRdWt;
		break;
	case 4: /* NDEF Write To Int */
		bufItem->buffer = &ndefData->ndefWriteToInt;
		break;
	case 5: /* NDEF Write Wt Int */
		bufItem->buffer = &ndefData->ndefWriteWtInt;
		break;
	default:
		bufItem->buffer = NULL;
		bufItem->length = 0;
		break;
	}

	/* Move to to next item for next call */
	wlcCapabilityItem++;

	return bufItem->buffer;
}

ReturnCode ndefRtdWlcCapabilityToRecord(const ndefType *type, ndefRecord *record)
{
	if ((type == NULL) || (type->id != NDEF_TYPE_ID_RTD_WLCCAP) || (record == NULL)) {
		return ERR_PARAM;
	}

	(void)ndefRecordReset(record);

	/* String type */
	(void)ndefRecordSetType(record, NDEF_TNF_RTD_WELL_KNOWN_TYPE, &bufTypeRtdWlcCapability);

	if (ndefRecordSetNdefType(record, type) != ERR_NONE) {
		return ERR_PARAM;
	}

	return ERR_NONE;
}

ReturnCode ndefRtdWlcCapabilityInit(ndefType *type, const ndefTypeRtdWlcCapability *param)
{
	ndefTypeRtdWlcCapability *ndefData;

	if ((type == NULL) || (param == NULL)) {
		return ERR_PARAM;
	}

	type->id = NDEF_TYPE_ID_RTD_WLCCAP;
	type->getPayloadLength = ndefRtdWlcCapabilityGetPayloadLength;
	type->getPayloadItem = ndefRtdWlcCapabilityGetPayloadItem;
	type->typeToRecord = ndefRtdWlcCapabilityToRecord;
	ndefData = &type->data.wlcCapability;

	(void)ST_MEMCPY(ndefData, param, sizeof(ndefTypeRtdWlcCapability));

	return ERR_NONE;
}

/* ------------------------------------------------------------------ */
/* WLC Status and Information (WLCSTAI)                                */
/* ------------------------------------------------------------------ */

static uint32_t ndefRtdWlcStatusInfoGetPayloadLength(const ndefType *type)
{
	uint32_t length;

	if ((type == NULL) || (type->id != NDEF_TYPE_ID_RTD_WLCSTAI)) {
		return 0;
	}

	const ndefTypeRtdWlcStatusInfo *ndefData = &type->data.wlcStatusInfo;

	length = sizeof(uint8_t); /* Control byte 1 */

	/* Count the number of bits set in the Control byte 1 to get the number of following bytes
	 */
	for (uint32_t i = 0; i < 8U; i++) {
		if ((ndefData->controlByte1 & (1U << i)) != 0U) {
			length += sizeof(uint8_t);
		}
	}

	return length;
}

static const uint8_t *ndefRtdWlcStatusInfoGetPayloadItem(const ndefType *type,
							 ndefConstBuffer *bufItem, bool begin)
{
	const ndefTypeRtdWlcStatusInfo *ndefData;

	if ((type == NULL) || (type->id != NDEF_TYPE_ID_RTD_WLCSTAI) || (bufItem == NULL)) {
		return NULL;
	}

	ndefData = &type->data.wlcStatusInfo;

	if (begin == true) {
		wlcStatusInfoItem = 0;
	}

	/* Prepare bufItem->length: Each field is 1 byte, except when complete */
	bufItem->length = sizeof(uint8_t);

	switch (wlcStatusInfoItem) {
	case 0: /* Control Byte 1 */
		bufItem->buffer = &ndefData->controlByte1;
		wlcStatusInfoItem++;
		break;
	case 1: /* Battery Level */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_BATTERY_LEVEL_MASK) != 0U) {
			bufItem->buffer = &ndefData->batteryLevel;
			wlcStatusInfoItem = 2;
			break;
		}
		/* fall through */
	case 2: /* Receive Power */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_RECEIVE_POWER_MASK) != 0U) {
			bufItem->buffer = &ndefData->receivePower;
			wlcStatusInfoItem = 3;
			break;
		}
		/* fall through */
	case 3: /* Receive Voltage */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_RECEIVE_VOLTAGE_MASK) != 0U) {
			bufItem->buffer = &ndefData->receiveVoltage;
			wlcStatusInfoItem = 4;
			break;
		}
		/* fall through */
	case 4: /* Receive Current */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_RECEIVE_CURRENT_MASK) != 0U) {
			bufItem->buffer = &ndefData->receiveCurrent;
			wlcStatusInfoItem = 5;
			break;
		}
		/* fall through */
	case 5: /* Battery temperature */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_TEMPERATURE_BATTERY_MASK) != 0U) {
			bufItem->buffer = &ndefData->temperatureBattery;
			wlcStatusInfoItem = 6;
			break;
		}
		/* fall through */
	case 6: /* WLCL temperature */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_TEMPERATURE_WLCL_MASK) != 0U) {
			bufItem->buffer = &ndefData->temperatureWlcl;
			wlcStatusInfoItem = 7;
			break;
		}
		/* fall through */
	case 7: /* RFU */
		if ((ndefData->controlByte1 & NDEF_WLC_STATUSINFO_CONTROLBYTE1_RFU_MASK) != 0U) {
			bufItem->buffer = &ndefData->rfu;
			wlcStatusInfoItem = 8;
			break;
		}
		/* fall through */
	case 8: /* Control byte 2 */
		if ((ndefData->controlByte1 &
		     NDEF_WLC_STATUSINFO_CONTROLBYTE1_CONTROL_BYTE_2_MASK) != 0U) {
			bufItem->buffer = &ndefData->controlByte2;
			wlcStatusInfoItem = 9;
			break;
		}
		/* fall through */
	default:
		bufItem->buffer = NULL;
		bufItem->length = 0;
		break;
	}

	return bufItem->buffer;
}

ReturnCode ndefRtdWlcStatusInfoToRecord(const ndefType *type, ndefRecord *record)
{
	if ((type == NULL) || (type->id != NDEF_TYPE_ID_RTD_WLCSTAI) || (record == NULL)) {
		return ERR_PARAM;
	}

	(void)ndefRecordReset(record);

	/* String type */
	(void)ndefRecordSetType(record, NDEF_TNF_RTD_WELL_KNOWN_TYPE, &bufTypeRtdWlcStatusInfo);

	if (ndefRecordSetNdefType(record, type) != ERR_NONE) {
		return ERR_PARAM;
	}

	return ERR_NONE;
}

ReturnCode ndefRtdWlcStatusInfoInit(ndefType *type, const ndefTypeRtdWlcStatusInfo *param)
{
	ndefTypeRtdWlcStatusInfo *ndefData;

	if ((type == NULL) || (param == NULL)) {
		return ERR_PARAM;
	}

	type->id = NDEF_TYPE_ID_RTD_WLCSTAI;
	type->getPayloadLength = ndefRtdWlcStatusInfoGetPayloadLength;
	type->getPayloadItem = ndefRtdWlcStatusInfoGetPayloadItem;
	type->typeToRecord = ndefRtdWlcStatusInfoToRecord;
	ndefData = &type->data.wlcStatusInfo;

	(void)ST_MEMCPY(ndefData, param, sizeof(ndefTypeRtdWlcStatusInfo));

	return ERR_NONE;
}
