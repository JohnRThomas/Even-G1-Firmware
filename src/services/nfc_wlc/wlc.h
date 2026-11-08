/*
 * NFC wireless charging (NFC Forum WLC) listener on the ST25DV: field voltage
 * measurement, charge supervision, the WLC listener state machine and the
 * NFC side of the production-test protocol.
 */
#ifndef G1_NFC_WLC_H_
#define G1_NFC_WLC_H_

#include <stdint.h>

int nfc_init(void);
uint8_t nfc_is_tag_type_ok(void);
uint32_t nfc_get_field_mv(void);
uint32_t chargerGetVdcRect(void);
int nfc_wlc_process(void);

#endif /* G1_NFC_WLC_H_ */
