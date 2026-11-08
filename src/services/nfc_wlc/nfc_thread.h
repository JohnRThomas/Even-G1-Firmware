/*
 * NFC event thread (nfc_thread.c).
 */
#ifndef G1_SERVICES_NFC_WLC_NFC_THREAD_H_
#define G1_SERVICES_NFC_WLC_NFC_THREAD_H_

#include <stdint.h>

void trigger_nfc_event(void);
int64_t nfc_deadline_set(uint32_t ms); /* returns the new deadline */
void nfc_thread_start(void);

#endif /* G1_SERVICES_NFC_WLC_NFC_THREAD_H_ */
