/*
 * NFC Forum WLC record types (ST RFAL NDEF ndef_type_wlc.h): the WLCCAP
 * capability and WLCSTAI status-and-information payloads and the functions
 * that turn them into NDEF records.
 */
#ifndef G1_NFC_WLC_NDEF_WLC_H_
#define G1_NFC_WLC_NDEF_WLC_H_

#include <stdint.h>

#include "services/nfc_wlc/ndef.h"

typedef struct {
	uint8_t wlcProtocolVersion;
	uint8_t wlcConfigModeReq;
	uint8_t wlcConfigWaitTimeRetry;
	uint8_t wlcConfigNegoWait;
	uint8_t wlcConfigRdConf;
	uint8_t capWtIntRfu;
	uint8_t capWtInt;
	uint8_t ndefRdWt;
	uint8_t ndefWriteToInt;
	uint8_t ndefWriteWtInt;
} ndefTypeRtdWlcCapability;

typedef struct {
	uint8_t controlByte1;
	uint8_t batteryLevel;
	uint8_t receivePower;
	uint8_t receiveVoltage;
	uint8_t receiveCurrent;
	uint8_t temperatureBattery;
	uint8_t temperatureWlcl;
	uint8_t rfu;
	uint8_t controlByte2;
} ndefTypeRtdWlcStatusInfo;

/* the Listen Control record is not used, but its 13-byte struct sizes the ndefType union */
typedef struct {
	uint8_t statusInfoErrorFlag;
	uint8_t statusInfoBatteryStatus;
	uint8_t statusInfoCnt;
	uint8_t wptConfigWptReq;
	uint8_t wptConfigWptDuration;
	uint8_t wptConfigInfoReq;
	uint8_t powerAdjReq;
	uint8_t batteryLevel;
	uint8_t drvInfoFlag;
	uint8_t drvInfoInt;
	uint8_t holdOffWtInt;
	uint8_t errorInfoError;
	uint8_t errorInfoTemperature;
} ndefTypeRtdWlcListenCtl;

#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_BATTERY_LEVEL_MASK       0x01U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_RECEIVE_POWER_MASK       0x02U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_RECEIVE_VOLTAGE_MASK     0x04U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_RECEIVE_CURRENT_MASK     0x08U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_TEMPERATURE_BATTERY_MASK 0x10U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_TEMPERATURE_WLCL_MASK    0x20U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_RFU_MASK                 0x40U
#define NDEF_WLC_STATUSINFO_CONTROLBYTE1_CONTROL_BYTE_2_MASK      0x80U

ReturnCode ndefRtdWlcCapabilityInit(ndefType *type, const ndefTypeRtdWlcCapability *param);
ReturnCode ndefRtdWlcCapabilityToRecord(const ndefType *type, ndefRecord *record);
ReturnCode ndefRtdWlcStatusInfoInit(ndefType *type, const ndefTypeRtdWlcStatusInfo *param);
ReturnCode ndefRtdWlcStatusInfoToRecord(const ndefType *type, ndefRecord *record);

#endif /* G1_NFC_WLC_NDEF_WLC_H_ */
