/*
 * NDEF records and messages: the subset of ST's RFAL NDEF library the WLC
 * listener needs (st_errno.h, ndef_buffer.h, nfc_utils.h, ndef_record.h and
 * ndef_message.h).  The well-known type dispatch is in ndef_types.h.
 */
#ifndef G1_NFC_WLC_NDEF_H_
#define G1_NFC_WLC_NDEF_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* st_errno.h */
typedef uint16_t ReturnCode;

#define ERR_NONE            ((ReturnCode)0U)
#define ERR_NOMEM           ((ReturnCode)1U)
#define ERR_PARAM           ((ReturnCode)7U)
#define ERR_PROTO           ((ReturnCode)11U)
#define ERR_INTERNAL        ((ReturnCode)12U)
#define ERR_NOT_IMPLEMENTED ((ReturnCode)15U)

/* ndef_buffer.h */
typedef struct {
	const uint8_t *buffer;
	uint32_t length;
} ndefConstBuffer;

typedef struct {
	uint8_t *buffer;
	uint32_t length;
} ndefBuffer;

typedef struct {
	const uint8_t *buffer;
	uint8_t length;
} ndefConstBuffer8;

#define ndefBufferIsInvalid(buf) (((buf)->buffer == NULL) && ((buf)->length != 0U))

/* nfc_utils.h */
#define GETU32(a)                                                                                  \
	(((uint32_t)(a)[0] << 24) | ((uint32_t)(a)[1] << 16) | ((uint32_t)(a)[2] << 8) |           \
	 ((uint32_t)(a)[3]))
#define ST_MEMCPY memcpy

/* ndef_record.h */
#define NDEF_RECORD_HEADER_LEN       7U
#define NDEF_SHORT_RECORD_LENGTH_MAX 255U

#define NDEF_TNF_EMPTY               0U
#define NDEF_TNF_RTD_WELL_KNOWN_TYPE 1U
#define NDEF_TNF_MASK                7U

#define ndefHeader(MB, ME, CF, SR, IL, TNF)                                                        \
	((((MB) & 1U) << 7U) | (((ME) & 1U) << 6U) | (((CF) & 1U) << 5U) | (((SR) & 1U) << 4U) |   \
	 (((IL) & 1U) << 3U) | ((uint8_t)(TNF) & NDEF_TNF_MASK))

#define ndefHeaderSR(record)  (((record)->header & 0x10U) >> 4)
#define ndefHeaderIL(record)  (((record)->header & 0x08U) >> 3)
#define ndefHeaderTNF(record) ((record)->header & NDEF_TNF_MASK)

#define ndefHeaderSetMB(record)         ((record)->header |= (1U << 7))
#define ndefHeaderSetME(record)         ((record)->header |= (1U << 6))
#define ndefHeaderSetIL(record)         ((record)->header |= (1U << 3))
#define ndefHeaderSetTNF(record, value) ((record)->header |= (uint8_t)(value) & NDEF_TNF_MASK)

#define ndefHeaderClearMB(record) ((record)->header &= 0x7FU)
#define ndefHeaderClearME(record) ((record)->header &= 0xBFU)
#define ndefHeaderClearIL(record) ((record)->header &= 0xF7U)

#define ndefHeaderSetValueSR(record, value)                                                        \
	do {                                                                                       \
		(record)->header &= 0xEFU;                                                         \
		(record)->header |= (((uint8_t)(value)) & 1U) << 4;                                \
	} while (0)

#define ndefHeaderIsSetSR(record) (ndefHeaderSR(record) == 1U)
#define ndefHeaderIsSetIL(record) (ndefHeaderIL(record) == 1U)

typedef struct ndefTypeStruct ndefType; /* well-known type, see ndef_types.h */
typedef struct ndefMessageStruct ndefMessage;

/* one NDEF record */
typedef struct ndefRecordStruct {
	uint8_t header;   /* MB:1 ME:1 CF:1 SR:1 IL:1 TNF:3 */
	uint8_t typeLength;
	uint8_t idLength;
	const uint8_t *type;
	const uint8_t *id;
	ndefConstBuffer bufPayload;
	const ndefType *ndeftype; /* well-known type data (payload generator) */
	struct ndefRecordStruct *next;
} ndefRecord;

/* ndef_message.h */
typedef struct {
	uint32_t length;
	uint32_t recordCount;
} ndefMessageInfo;

struct ndefMessageStruct {
	ndefRecord *record;
	ndefMessageInfo info;
};

#define ndefMessageGetFirstRecord(message) (((message) == NULL) ? NULL : (message)->record)
#define ndefMessageGetNextRecord(record)   (((record) == NULL) ? NULL : (record)->next)

/* ndef_record.c */
ReturnCode ndefRecordReset(ndefRecord *record);
uint32_t ndefRecordGetHeaderLength(const ndefRecord *record);
uint32_t ndefRecordGetPayloadLength(const ndefRecord *record);
uint32_t ndefRecordGetLength(const ndefRecord *record);
ReturnCode ndefRecordSetType(ndefRecord *record, uint8_t tnf, const ndefConstBuffer8 *bufType);
ReturnCode ndefRecordSetId(ndefRecord *record, const ndefConstBuffer8 *bufId);
ReturnCode ndefRecordSetPayload(ndefRecord *record, const ndefConstBuffer *bufPayload);
ReturnCode ndefRecordGetPayload(const ndefRecord *record, ndefConstBuffer *bufPayload);
const uint8_t *ndefRecordGetPayloadItem(const ndefRecord *record, ndefConstBuffer *bufPayloadItem,
					bool begin);
ReturnCode ndefRecordDecode(const ndefConstBuffer *bufPayload, ndefRecord *record);
ReturnCode ndefRecordEncodeHeader(const ndefRecord *record, ndefBuffer *bufHeader);
ReturnCode ndefRecordEncode(const ndefRecord *record, ndefBuffer *bufRecord);

/* ndef_message.c */
ReturnCode ndefMessageInit(ndefMessage *message);
ReturnCode ndefMessageGetInfo(const ndefMessage *message, ndefMessageInfo *info);
ReturnCode ndefMessageAppend(ndefMessage *message, ndefRecord *record);
ReturnCode ndefMessageEncode(const ndefMessage *message, ndefBuffer *bufPayload);
ReturnCode ndefMessageDecode(const ndefConstBuffer *bufPayload, ndefMessage *message);

#endif /* G1_NFC_WLC_NDEF_H_ */
