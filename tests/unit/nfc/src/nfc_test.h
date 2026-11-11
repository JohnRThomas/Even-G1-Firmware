#ifndef G1_UNIT_NFC_TEST_H_
#define G1_UNIT_NFC_TEST_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

#include "st25dv_emul.h"

#define TAG_E G1_ST25DV_EMUL

extern uint32_t nfc_field_mv;

/* Every nfc test's before calls this. */
void nfc_wlc_test_reset(void);

void nfc_pt_test_reset(void);

void nfc_test_set_pin_mv(uint32_t mv);

uint8_t nfc_test_sum8(const uint8_t *buf, size_t n);

#endif /* G1_UNIT_NFC_TEST_H_ */
