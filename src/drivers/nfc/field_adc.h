/*
 * NFC field voltage ADC (field_adc.c).
 */
#ifndef G1_DRIVERS_NFC_FIELD_ADC_H_
#define G1_DRIVERS_NFC_FIELD_ADC_H_

#include <stdint.h>

int nfc_field_adc_init(void);
/* one sample in mV; 0 when the reading failed or is out of the 0..1200 mV range */
uint32_t nfc_field_adc_read_mv(void);

#endif /* G1_DRIVERS_NFC_FIELD_ADC_H_ */
