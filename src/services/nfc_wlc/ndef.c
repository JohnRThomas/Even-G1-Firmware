/*
 * NDEF record and message encoder, from ST's RFAL NDEF library:
 *   ndef_record.c   ndefRecordReset, ndefRecordGetHeaderLength,
 *                   ndefRecordGetPayloadLength, ndefRecordGetLength,
 *                   ndefRecordSetType/Id/Payload, ndefRecordGetPayload,
 *                   ndefRecordGetPayloadItem, ndefRecordDecode,
 *                   ndefRecordEncodeHeader, ndefRecordEncode (+ static
 *                   ndefRecordEncodePayload)
 *   ndef_message.c  ndefAllocRecord, ndefMessageInit, ndefMessageGetInfo,
 *                   ndefMessageAppend, ndefMessageEncode, ndefMessageDecode
 *   ndef_types.c    ndefTypeToRecord, ndefRecordSetNdefType
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/nfc_wlc/ndef.h"
#include "services/nfc_wlc/ndef_types.h"

/* ------------------------------------------------------------------ */
/* ndef_record.c                                                       */
/* ------------------------------------------------------------------ */

ReturnCode ndefRecordReset(ndefRecord *record)
{
	ndefConstBuffer8 bufEmpty8 = {NULL, 0};
	ndefConstBuffer bufEmpty = {NULL, 0};

	if (record == NULL) {
		return ERR_PARAM;
	}

	/* Set the MB and ME bits */
	record->header = ndefHeader(1U, 1U, 0U, 0U, 0U, NDEF_TNF_EMPTY);

	(void)ndefRecordSetType(record, NDEF_TNF_EMPTY, &bufEmpty8);
	(void)ndefRecordSetId(record, &bufEmpty8);

	/* Set the SR bit */
	(void)ndefRecordSetPayload(record, &bufEmpty);

	record->ndeftype = NULL;
	record->next = NULL;

	return ERR_NONE;
}

uint32_t ndefRecordGetHeaderLength(const ndefRecord *record)
{
	uint32_t length;

	if (record == NULL) {
		return 0;
	}

	length = sizeof(uint8_t); /* header (MB:1 + ME:1 + CF:1 + SR:1 + IL:1 + TNF:3 => 8 bits) */
	length += sizeof(uint8_t); /* Type length */
	if (ndefHeaderIsSetSR(record)) {
		length += sizeof(uint8_t); /* Short record */
	} else {
		length += sizeof(uint32_t); /* Standard record */
	}
	if (ndefHeaderIsSetIL(record)) {
		length += sizeof(uint8_t); /* Id length */
	}
	length += record->typeLength; /* Type */
	length += record->idLength; /* Id */

	return length;
}

uint32_t ndefRecordGetPayloadLength(const ndefRecord *record)
{
	uint32_t payloadLength;

	if (record == NULL) {
		return 0;
	}

	if ((record->ndeftype != NULL) && (record->ndeftype->getPayloadLength != NULL)) {
		payloadLength = record->ndeftype->getPayloadLength(record->ndeftype);
	} else {
		payloadLength = record->bufPayload.length;
	}

	return payloadLength;
}

uint32_t ndefRecordGetLength(const ndefRecord *record)
{
	uint32_t length;

	length = ndefRecordGetHeaderLength(record); /* Header */
	length += ndefRecordGetPayloadLength(record); /* Payload */

	return length;
}

ReturnCode ndefRecordSetType(ndefRecord *record, uint8_t tnf, const ndefConstBuffer8 *bufType)
{
	if ((record == NULL) || (bufType == NULL) || ndefBufferIsInvalid(bufType)) {
		return ERR_PARAM;
	}

	ndefHeaderSetTNF(record, tnf);
	record->typeLength = bufType->length;
	record->type = bufType->buffer;

	return ERR_NONE;
}

ReturnCode ndefRecordSetId(ndefRecord *record, const ndefConstBuffer8 *bufId)
{
	if ((record == NULL) || (bufId == NULL) || ndefBufferIsInvalid(bufId)) {
		return ERR_PARAM;
	}

	if (bufId->buffer != NULL) {
		ndefHeaderSetIL(record);
	} else {
		ndefHeaderClearIL(record);
	}
	record->id = bufId->buffer;
	record->idLength = bufId->length;

	return ERR_NONE;
}

ReturnCode ndefRecordSetPayload(ndefRecord *record, const ndefConstBuffer *bufPayload)
{
	if ((record == NULL) || (bufPayload == NULL) || ndefBufferIsInvalid(bufPayload)) {
		return ERR_PARAM;
	}

	ndefHeaderSetValueSR(record, (bufPayload->length <= NDEF_SHORT_RECORD_LENGTH_MAX) ? 1 : 0);
	record->bufPayload.buffer = bufPayload->buffer;
	record->bufPayload.length = bufPayload->length;

	return ERR_NONE;
}

ReturnCode ndefRecordGetPayload(const ndefRecord *record, ndefConstBuffer *bufPayload)
{
	if ((record == NULL) || (bufPayload == NULL)) {
		return ERR_PARAM;
	}

	bufPayload->buffer = record->bufPayload.buffer;
	bufPayload->length = record->bufPayload.length;

	return ERR_NONE;
}

const uint8_t *ndefRecordGetPayloadItem(const ndefRecord *record, ndefConstBuffer *bufPayloadItem,
					bool begin)
{
	if ((record == NULL) || (bufPayloadItem == NULL)) {
		return NULL;
	}

	bufPayloadItem->buffer = NULL;
	bufPayloadItem->length = 0;

	if ((record->ndeftype != NULL) && (record->ndeftype->getPayloadItem != NULL)) {
		record->ndeftype->getPayloadItem(record->ndeftype, bufPayloadItem, begin);
	} else {
		if (begin == true) {
			(void)ndefRecordGetPayload(record, bufPayloadItem);
		}
	}

	return bufPayloadItem->buffer;
}

ReturnCode ndefRecordDecode(const ndefConstBuffer *bufPayload, ndefRecord *record)
{
	uint32_t offset;

	if ((bufPayload == NULL) || (bufPayload->buffer == NULL) || (record == NULL)) {
		return ERR_PARAM;
	}

	if (ndefRecordReset(record) != ERR_NONE) {
		return ERR_INTERNAL;
	}

	/* Get "header" byte */
	offset = 0;
	if ((offset + sizeof(uint8_t)) > bufPayload->length) {
		return ERR_PROTO;
	}
	record->header = bufPayload->buffer[offset];
	offset++;

	/* Get Type length */
	if ((offset + sizeof(uint8_t)) > bufPayload->length) {
		return ERR_PROTO;
	}
	record->typeLength = bufPayload->buffer[offset];
	offset++;

	/* Decode Payload length */
	if (ndefHeaderIsSetSR(record)) {
		/* Short record */
		if ((offset + sizeof(uint8_t)) > bufPayload->length) {
			return ERR_PROTO;
		}
		record->bufPayload.length = bufPayload->buffer[offset]; /* length stored on a single
									   byte for Short Record */
		offset++;
	} else {
		/* Standard record */
		if ((offset + sizeof(uint32_t)) > bufPayload->length) {
			return ERR_PROTO;
		}
		record->bufPayload.length = GETU32(&bufPayload->buffer[offset]);
		offset += sizeof(uint32_t);
	}

	/* Get Id length */
	if (ndefHeaderIsSetIL(record)) {
		if ((offset + sizeof(uint8_t)) > bufPayload->length) {
			return ERR_PROTO;
		}
		record->idLength = bufPayload->buffer[offset];
		offset++;
	} else {
		record->idLength = 0;
	}

	/* Get Type */
	if (record->typeLength > 0U) {
		if ((offset + record->typeLength) > bufPayload->length) {
			return ERR_PROTO;
		}
		record->type = &bufPayload->buffer[offset];
		offset += record->typeLength;
	} else {
		record->type = NULL;
	}

	/* Get Id */
	if (record->idLength > 0U) {
		if ((offset + record->idLength) > bufPayload->length) {
			return ERR_PROTO;
		}
		record->id = &bufPayload->buffer[offset];
		offset += record->idLength;
	} else {
		record->id = NULL;
	}

	/* Get Payload */
	if (record->bufPayload.length > 0U) {
		if ((offset + record->bufPayload.length) > bufPayload->length) {
			return ERR_PROTO;
		}
		record->bufPayload.buffer = &bufPayload->buffer[offset];
	} else {
		record->bufPayload.buffer = NULL;
	}

	record->next = NULL;

	return ERR_NONE;
}

ReturnCode ndefRecordEncodeHeader(const ndefRecord *record, ndefBuffer *bufHeader)
{
	uint32_t offset;
	uint32_t payloadLength;

	if ((record == NULL) || (bufHeader == NULL) || (bufHeader->buffer == NULL)) {
		return ERR_PARAM;
	}

	if (bufHeader->length < NDEF_RECORD_HEADER_LEN) {
		bufHeader->length = NDEF_RECORD_HEADER_LEN;
		return ERR_NOMEM;
	}

	/* Start encoding the record */
	offset = 0;
	bufHeader->buffer[offset] = record->header;
	offset++;

	/* Set Type length */
	bufHeader->buffer[offset] = record->typeLength;
	offset++;

	/* Encode Payload length */
	payloadLength = ndefRecordGetPayloadLength(record);
	if (payloadLength <= NDEF_SHORT_RECORD_LENGTH_MAX) {
		/* Short record */
		bufHeader->buffer[offset] = (uint8_t)payloadLength;
		offset++;
	} else {
		/* Standard record */
		bufHeader->buffer[offset] = (uint8_t)(payloadLength >> 24);
		offset++;
		bufHeader->buffer[offset] = (uint8_t)(payloadLength >> 16);
		offset++;
		bufHeader->buffer[offset] = (uint8_t)(payloadLength >> 8);
		offset++;
		bufHeader->buffer[offset] = (uint8_t)payloadLength;
		offset++;
	}

	/* Encode Id length */
	if (ndefHeaderIsSetIL(record)) {
		bufHeader->buffer[offset] = record->idLength;
		offset++;
	}

	bufHeader->length = offset;

	return ERR_NONE;
}

static ReturnCode ndefRecordEncodePayload(const ndefRecord *record, ndefBuffer *bufPayload)
{
	uint32_t payloadLength;
	uint32_t offset;
	bool begin;
	ndefConstBuffer bufPayloadItem;

	if (bufPayload == NULL) {
		return ERR_PARAM;
	}

	payloadLength = ndefRecordGetPayloadLength(record);
	if (payloadLength > bufPayload->length) {
		return ERR_NOMEM;
	}

	begin = true;
	offset = 0;
	while (ndefRecordGetPayloadItem(record, &bufPayloadItem, begin) != NULL) {
		begin = false;
		if (bufPayloadItem.length > 0U) {
			ST_MEMCPY(&bufPayload->buffer[offset], bufPayloadItem.buffer,
				  bufPayloadItem.length);
		}
		offset += bufPayloadItem.length;
	}

	bufPayload->length = offset;

	return ERR_NONE;
}

ReturnCode ndefRecordEncode(const ndefRecord *record, ndefBuffer *bufRecord)
{
	ReturnCode err;
	ndefBuffer bufHeader;
	ndefBuffer bufPayload;
	uint32_t offset;

	if ((record == NULL) || (bufRecord == NULL) || (bufRecord->buffer == NULL)) {
		return ERR_PARAM;
	}

	if (bufRecord->length < ndefRecordGetLength(record)) {
		bufRecord->length = ndefRecordGetLength(record);
		return ERR_NOMEM;
	}

	/* Encode header at the beginning of buffer provided */
	bufHeader = *bufRecord; /* Copy ndefBuffer fields */
	err = ndefRecordEncodeHeader(record, &bufHeader);
	if (err != ERR_NONE) {
		return err;
	}

	offset = bufHeader.length;

	/* Set Type */
	if (record->typeLength > 0U) {
		(void)ST_MEMCPY(&bufRecord->buffer[offset], record->type, record->typeLength);
		offset += record->typeLength;
	}

	/* Set Id */
	if (record->idLength > 0U) {
		(void)ST_MEMCPY(&bufRecord->buffer[offset], record->id, record->idLength);
		offset += record->idLength;
	}

	/* Set Payload */
	bufPayload.buffer = &bufRecord->buffer[offset];
	bufPayload.length = bufRecord->length - offset;
	err = ndefRecordEncodePayload(record, &bufPayload);
	if (err != ERR_NONE) {
		return err;
	}

	bufRecord->length = offset + bufPayload.length;

	return ERR_NONE;
}

/* ------------------------------------------------------------------ */
/* ndef_message.c                                                      */
/* ------------------------------------------------------------------ */

#define NDEF_MAX_RECORD 10U

static uint8_t ndefRecordPoolIndex = 0;

static ndefRecord *ndefAllocRecord(void)
{
	static ndefRecord ndefRecordPool[NDEF_MAX_RECORD];

	if (ndefRecordPoolIndex >= NDEF_MAX_RECORD) {
		return NULL;
	}

	return &ndefRecordPool[ndefRecordPoolIndex++];
}

ReturnCode ndefMessageInit(ndefMessage *message)
{
	if (message == NULL) {
		return ERR_PARAM;
	}

	message->record = NULL;
	message->info.length = 0;
	message->info.recordCount = 0;

	ndefRecordPoolIndex = 0;

	return ERR_NONE;
}

ReturnCode ndefMessageGetInfo(const ndefMessage *message, ndefMessageInfo *info)
{
	ndefRecord *record;
	uint32_t length = 0;
	uint32_t recordCount = 0;

	if ((message == NULL) || (info == NULL)) {
		return ERR_PARAM;
	}

	record = message->record;

	while (record != NULL) {
		length += ndefRecordGetLength(record);
		recordCount++;
		record = record->next;
	}

	info->length = length;
	info->recordCount = recordCount;

	return ERR_NONE;
}

ReturnCode ndefMessageAppend(ndefMessage *message, ndefRecord *record)
{
	if ((message == NULL) || (record == NULL)) {
		return ERR_PARAM;
	}

	/* Clear the Message Begin bit */
	ndefHeaderClearMB(record);

	/* Record is appended so it is the last in the list, set the Message End bit */
	ndefHeaderSetME(record);

	record->next = NULL;

	if (message->record == NULL) {
		/* Set the Message Begin bit for the first record only */
		ndefHeaderSetMB(record);

		message->record = record;
	} else {
		ndefRecord *current = message->record;

		/* Go through the list of records */
		while (current->next != NULL) {
			current = current->next;
		}

		/* Clear the Message End bit to the record before the one being appended */
		ndefHeaderClearME(current);

		/* Append to the last record */
		current->next = record;
	}

	message->info.length += ndefRecordGetLength(record);
	message->info.recordCount += 1U;

	return ERR_NONE;
}

ReturnCode ndefMessageEncode(const ndefMessage *message, ndefBuffer *bufPayload)
{
	ReturnCode err;
	ndefMessageInfo info = {0};
	ndefRecord *record;
	uint32_t offset;
	uint32_t remainingLength;

	if ((bufPayload == NULL) || (bufPayload->buffer == NULL)) {
		return ERR_PARAM;
	}

	err = ndefMessageGetInfo(message, &info);
	if ((err != ERR_NONE) || (bufPayload->length < info.length)) {
		bufPayload->length = info.length;
		return ERR_NOMEM;
	}

	/* Get the first record */
	record = ndefMessageGetFirstRecord(message);
	offset = 0;
	remainingLength = bufPayload->length;

	while (record != NULL) {
		ndefBuffer bufRecord;

		bufRecord.buffer = &bufPayload->buffer[offset];
		bufRecord.length = remainingLength;
		err = ndefRecordEncode(record, &bufRecord);
		if (err != ERR_NONE) {
			bufPayload->length = info.length;
			return err;
		}
		offset += bufRecord.length;
		remainingLength -= bufRecord.length;

		record = ndefMessageGetNextRecord(record);
	}

	bufPayload->length = offset;

	return ERR_NONE;
}

/* unused */
ReturnCode ndefMessageDecode(const ndefConstBuffer *bufPayload, ndefMessage *message)
{
	ReturnCode err;
	uint32_t offset;

	if ((bufPayload == NULL) || (bufPayload->buffer == NULL)) {
		return ERR_PARAM;
	}

	err = ndefMessageInit(message);
	if (err != ERR_NONE) {
		return err;
	}

	offset = 0;
	while (offset < bufPayload->length) {
		ndefConstBuffer bufRecord;
		ndefRecord *record = ndefAllocRecord();

		if (record == NULL) {
			return ERR_NOMEM;
		}
		bufRecord.buffer = &bufPayload->buffer[offset];
		bufRecord.length = bufPayload->length - offset;
		err = ndefRecordDecode(&bufRecord, record);
		if (err != ERR_NONE) {
			return err;
		}
		offset += ndefRecordGetLength(record);

		err = ndefMessageAppend(message, record);
		if (err != ERR_NONE) {
			return err;
		}
	}

	return ERR_NONE;
}

/* ------------------------------------------------------------------ */
/* ndef_types.c                                                        */
/* ------------------------------------------------------------------ */

ReturnCode ndefTypeToRecord(const ndefType *type, ndefRecord *record)
{
	if (type == NULL) {
		return ERR_PARAM;
	}

	if (type->typeToRecord != NULL) {
		return type->typeToRecord(type, record);
	}

	return ERR_NOT_IMPLEMENTED;
}

ReturnCode ndefRecordSetNdefType(ndefRecord *record, const ndefType *type)
{
	uint32_t payloadLength;

	if ((record == NULL) || (type == NULL) || (type->id == NDEF_TYPE_ID_NONE) ||
	    (type->id > NDEF_TYPE_ID_COUNT) || (type->getPayloadLength == NULL) ||
	    (type->getPayloadItem == NULL) || (type->typeToRecord == NULL)) {
		return ERR_PARAM;
	}

	record->ndeftype = type;

	/* Set Short Record bit accordingly */
	payloadLength = ndefRecordGetPayloadLength(record);
	ndefHeaderSetValueSR(record, (payloadLength <= NDEF_SHORT_RECORD_LENGTH_MAX) ? 1 : 0);

	return ERR_NONE;
}
