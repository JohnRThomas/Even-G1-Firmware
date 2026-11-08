/*
 * Type 5 tag access on the ST25DV, from ST's lib_NDEF tagtype5_wrapper.c:
 * capability container (CC file) read/write, NDEF TLV detection and write,
 * tag formatting, plus nfc_write_ndef(), which formats the tag when the
 * first write fails.  BSP_NFCTAG_GetByteSize() is the ST25DV BSP hook the
 * wrapper calls for the tag size.
 */
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "drivers/nfc/st25dv.h"
#include "services/nfc_wlc/tagtype5.h"
#include "drivers/nfc/eeprom_st25dv.h"

#define NFCT5_MAGICNUMBER_E1_CCFILE 0xE1
#define NFCT5_MAGICNUMBER_E2_CCFILE 0xE2
#define NFCT5_EXTENDED_CCFILE       0x00
#define NFCT5_NDEF_MSG_TLV          0x03
#define NFCT5_PROPRIETARY_TLV       0xFD
#define NFCT5_TERMINATOR_TLV        0xFE
#define NFCT5_3_BYTES_L_TLV         0xFF

typedef enum {
	TT5_NO_NDEF = 0,
	TT5_INITIALIZED,
	TT5_READ_WRITE,
	TT5_READ,
} TT5_State;

/* capability container as read from the tag */
typedef struct {
	uint8_t MagicNumber;
	uint8_t Version;
	uint8_t MemorySize;
	uint8_t TT5Tag;
	uint8_t rsved1;
	uint8_t rsved2;
	uint16_t ExtMemorySize;
	TT5_State State;
	uint32_t NDEF_offset;
} sCCFileInfo;

typedef struct {
	uint8_t Type;
	uint8_t Length;
	uint16_t Length16;
} TT5_TLV_t;

static sCCFileInfo CCFileStruct;

uint32_t BSP_NFCTAG_GetByteSize(void)
{
	struct st25dv_mem_size mem_size;

	st25dv_read_mem_size(global_st25dv_context_ptr, &mem_size);
	return (mem_size.blk_size + 1) * (mem_size.mem_size + 1);
}

int NfcType5_ReadCCFile(uint8_t *pCCBuffer)
{
	int ret_value;

	/* Read 4 bytes of CC File */
	ret_value = eeprom_st25dv_read(global_st25dv_context_ptr, 0, pCCBuffer, 4);

	/* If extended memory reads the next 4 bytes */
	if (pCCBuffer[2] == NFCT5_EXTENDED_CCFILE) {
		if (ret_value != 0) {
			return NDEF_ERROR;
		}
		ret_value = eeprom_st25dv_read(global_st25dv_context_ptr, 4, pCCBuffer + 4, 4);
	}

	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	return NDEF_OK;
}

int NfcType5_NDEFDetection(void)
{
	uint8_t acc_buffer[8];
	TT5_TLV_t tlv_detect;
	int16_t status;
	uint32_t memory_size;

	CCFileStruct.State = TT5_NO_NDEF;

	/* Read CCFile */
	status = NfcType5_ReadCCFile(acc_buffer);
	if (status != NDEF_OK) {
		return status;
	}

	/* Check Byte 0 is equal to magic number */
	if ((acc_buffer[0] != NFCT5_MAGICNUMBER_E1_CCFILE) &&
	    (acc_buffer[0] != NFCT5_MAGICNUMBER_E2_CCFILE)) {
		return NDEF_ERROR_NOT_FORMATED;
	}
	/* Check Version and Access Condition */
	if ((acc_buffer[1] & 0xFC) != 0x40) {
		return NDEF_ERROR_NOT_FORMATED;
	}

	/* Check if CCFile is on 4 Bytes or 8 Bytes */
	CCFileStruct.MemorySize = acc_buffer[2];
	if (acc_buffer[2] == NFCT5_EXTENDED_CCFILE) {
		CCFileStruct.ExtMemorySize = sys_get_be16(&acc_buffer[6]);
		memory_size = CCFileStruct.ExtMemorySize;
		CCFileStruct.NDEF_offset = 8;
	} else {
		CCFileStruct.ExtMemorySize = 0;
		memory_size = CCFileStruct.MemorySize;
		CCFileStruct.NDEF_offset = 4;
	}

	CCFileStruct.MagicNumber = acc_buffer[0];
	CCFileStruct.Version = acc_buffer[1];
	CCFileStruct.TT5Tag = acc_buffer[3];

	/* Search for position of NDEF TLV in memory and tag status */
	while ((eeprom_st25dv_read(global_st25dv_context_ptr, CCFileStruct.NDEF_offset, &tlv_detect,
				   sizeof(TT5_TLV_t)) == 0) &&
	       (CCFileStruct.NDEF_offset < memory_size)) {
		/* Detect first NDEF Message in memory */
		if (tlv_detect.Type == NFCT5_NDEF_MSG_TLV) {
			if (tlv_detect.Length == 0x00) {
				CCFileStruct.State = TT5_INITIALIZED;
			} else if (CCFileStruct.Version & 0x3) {
				CCFileStruct.State = TT5_READ;
			} else {
				CCFileStruct.State = TT5_READ_WRITE;
			}
			return NDEF_OK;
		}
		/* If Proprietary NDEF jump to end of proprietary message */
		if (tlv_detect.Type == NFCT5_PROPRIETARY_TLV) {
			if (tlv_detect.Length == NFCT5_3_BYTES_L_TLV) {
				CCFileStruct.NDEF_offset += tlv_detect.Length16;
			} else {
				CCFileStruct.NDEF_offset += tlv_detect.Length;
			}
			continue;
		}
		break;
	}

	return NDEF_ERROR_NOT_FORMATED;
}

int NfcType5_WriteTLV(uint8_t type, uint32_t Length, const uint8_t *pData)
{
	uint8_t TLV_Header[4];
	uint8_t NDEF_end = NFCT5_TERMINATOR_TLV;
	uint8_t TLVHeaderLength;
	uint32_t maxLength;
	uint32_t offset;
	int ret_value;

	maxLength = BSP_NFCTAG_GetByteSize() - CCFileStruct.NDEF_offset - 1 -
		    ((Length >= 0xFF) ? 4 : 2);
	if (Length > maxLength) {
		return NDEF_ERROR_MEMORY_TAG;
	}

	/* Detect NDEF in tag */
	if (NfcType5_NDEFDetection() != NDEF_OK) {
		return NDEF_ERROR;
	}

	TLV_Header[0] = type;
	if (Length >= 0xFF) {
		TLV_Header[1] = NFCT5_3_BYTES_L_TLV;
		*(uint16_t *)&TLV_Header[2] = sys_cpu_to_be16(Length);
		TLVHeaderLength = 4;
	} else {
		TLV_Header[1] = (uint8_t)Length;
		TLVHeaderLength = 2;
	}

	offset = CCFileStruct.NDEF_offset;
	ret_value =
		eeprom_st25dv_write(global_st25dv_context_ptr, offset, TLV_Header, TLVHeaderLength);
	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	offset += TLVHeaderLength;
	ret_value = eeprom_st25dv_write(global_st25dv_context_ptr, offset, pData, Length);
	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	offset += Length;
	ret_value = eeprom_st25dv_write(global_st25dv_context_ptr, offset, &NDEF_end, 1);
	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	return NDEF_OK;
}

int NfcType5_WriteCCFile(const uint8_t *pCCBuffer)
{
	int ret_value;

	/* Write first block of CCFile */
	ret_value = eeprom_st25dv_write(global_st25dv_context_ptr, 0, pCCBuffer, 4);

	/* If extended memory writes the next 4 bytes */
	if (pCCBuffer[2] == NFCT5_EXTENDED_CCFILE) {
		if (ret_value != 0) {
			return NDEF_ERROR;
		}
		ret_value = eeprom_st25dv_write(global_st25dv_context_ptr, 4, pCCBuffer + 4, 4);
	}

	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	return NDEF_OK;
}

int NfcType5_TT5Init(void)
{
	int ret_value;
	int16_t status;
	uint8_t accbuffer[8];
	uint8_t cdata;

	/* Prepare buffer to update CCFile */
	accbuffer[0] = CCFileStruct.MagicNumber;
	accbuffer[1] = CCFileStruct.Version;
	accbuffer[2] = CCFileStruct.MemorySize;
	accbuffer[3] = CCFileStruct.TT5Tag;
	CCFileStruct.NDEF_offset = 0x04;

	/* If extended memory prepare the length bytes */
	if (CCFileStruct.MemorySize == NFCT5_EXTENDED_CCFILE) {
		sys_put_be16(CCFileStruct.ExtMemorySize, &accbuffer[6]);
		CCFileStruct.NDEF_offset = 0x08;
	}

	/* Update CCFile */
	status = NfcType5_WriteCCFile(accbuffer);
	if (status != NDEF_OK) {
		return status;
	}

	/* Update NDEF TLV for INITIALIZED state */
	/* Update T */
	cdata = NFCT5_NDEF_MSG_TLV;
	ret_value =
		eeprom_st25dv_write(global_st25dv_context_ptr, CCFileStruct.NDEF_offset, &cdata, 1);
	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	/* Update L */
	cdata = 0x00;
	ret_value = eeprom_st25dv_write(global_st25dv_context_ptr, CCFileStruct.NDEF_offset + 1,
					&cdata, 1);
	if (ret_value != 0) {
		return NDEF_ERROR;
	}

	return NDEF_OK;
}

int NfcType5_WriteNDEF(uint16_t Length, const uint8_t *pData)
{
	return NfcType5_WriteTLV(NFCT5_NDEF_MSG_TLV, Length, pData);
}

int nfc_write_ndef(const uint8_t *pData, uint32_t Length)
{
	int status;

	if (NfcType5_WriteNDEF(Length, pData) != NDEF_ERROR) {
		return NDEF_OK;
	}

	/* not formatted: write a fresh CC file (E1 40 40 01) and retry */
	CCFileStruct.MagicNumber = NFCT5_MAGICNUMBER_E1_CCFILE;
	CCFileStruct.Version = 0x40;
	CCFileStruct.MemorySize = 0x40;
	CCFileStruct.TT5Tag = 0x01;
	status = NfcType5_TT5Init();
	NfcType5_WriteNDEF(Length, pData);

	return status;
}
