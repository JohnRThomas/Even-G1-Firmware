/*
 * NDEF well-known type dispatch (ST RFAL NDEF ndef_types.h): the ndefType
 * object that carries a typed payload and the callbacks that encode it into
 * a record.  Only the WLC payloads are present in the union.
 */
#ifndef G1_NFC_WLC_NDEF_TYPES_H_
#define G1_NFC_WLC_NDEF_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

#include "services/nfc_wlc/ndef.h"
#include "services/nfc_wlc/ndef_wlc.h"

typedef enum {
	NDEF_TYPE_ID_NONE = 0,
	NDEF_TYPE_ID_FLAT,
	NDEF_TYPE_ID_EMPTY,
	NDEF_TYPE_ID_RTD_DEVICE_INFO,
	NDEF_TYPE_ID_RTD_TEXT,
	NDEF_TYPE_ID_RTD_URI,
	NDEF_TYPE_ID_RTD_AAR,
	NDEF_TYPE_ID_RTD_WLCCAP,
	NDEF_TYPE_ID_RTD_WLCSTAI,
	NDEF_TYPE_ID_RTD_WLCINFO,
	NDEF_TYPE_ID_RTD_WLCCTL,
	NDEF_TYPE_ID_RTD_WPCWLC,
	NDEF_TYPE_ID_RTD_TNEP_SERVICE_PARAMETER,
	NDEF_TYPE_ID_RTD_TNEP_SERVICE_SELECT,
	NDEF_TYPE_ID_RTD_TNEP_STATUS,
	NDEF_TYPE_ID_MEDIA,
	NDEF_TYPE_ID_BLUETOOTH_BREDR,
	NDEF_TYPE_ID_BLUETOOTH_LE,
	NDEF_TYPE_ID_BLUETOOTH_SECURE_BREDR,
	NDEF_TYPE_ID_BLUETOOTH_SECURE_LE,
	NDEF_TYPE_ID_MEDIA_VCARD,
	NDEF_TYPE_ID_MEDIA_WIFI,
	NDEF_TYPE_ID_COUNT
} ndefTypeId;

struct ndefTypeStruct {
	ndefTypeId id;
	uint32_t (*getPayloadLength)(const ndefType *type);
	const uint8_t *(*getPayloadItem)(const ndefType *type, ndefConstBuffer *item, bool begin);
	ReturnCode (*typeToRecord)(const ndefType *type, ndefRecord *record);
	union {
		ndefTypeRtdWlcCapability wlcCapability;
		ndefTypeRtdWlcStatusInfo wlcStatusInfo;
		ndefTypeRtdWlcListenCtl wlcListenCtl;
		uint8_t reserved;
	} data;
};

/* ndef_types.c */
ReturnCode ndefTypeToRecord(const ndefType *type, ndefRecord *record);
ReturnCode ndefRecordSetNdefType(ndefRecord *record, const ndefType *type);

#endif /* G1_NFC_WLC_NDEF_TYPES_H_ */
