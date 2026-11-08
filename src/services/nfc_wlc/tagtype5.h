/*
 * NFC Forum type 5 tag layout on the ST25DV (ST lib_NDEF tagtype5_wrapper.c):
 * capability container, NDEF TLV and the NDEF message write.
 */
#ifndef G1_NFC_WLC_TAGTYPE5_H_
#define G1_NFC_WLC_TAGTYPE5_H_

#include <stdint.h>

#define NDEF_OK                 0
#define NDEF_ERROR              (-1)
#define NDEF_ERROR_MEMORY_TAG   (-2)
#define NDEF_ERROR_NOT_FORMATED (-5)

uint32_t BSP_NFCTAG_GetByteSize(void);
int NfcType5_ReadCCFile(uint8_t *pCCBuffer);
int NfcType5_NDEFDetection(void);
int NfcType5_WriteTLV(uint8_t type, uint32_t Length, const uint8_t *pData);
int NfcType5_WriteCCFile(const uint8_t *pCCBuffer);
int NfcType5_TT5Init(void);
int NfcType5_WriteNDEF(uint16_t Length, const uint8_t *pData);

/* writes the NDEF message, formatting the tag first when it has no valid CC file */
int nfc_write_ndef(const uint8_t *pData, uint32_t Length);

#endif /* G1_NFC_WLC_TAGTYPE5_H_ */
