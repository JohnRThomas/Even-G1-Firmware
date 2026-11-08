/*
 * Event reports to the phone (report.c).
 */
#ifndef G1_COMM_BLE_REPORT_H_
#define G1_COMM_BLE_REPORT_H_

#include <stdint.h>

extern uint8_t last_ble_cmd; /* pending 0xf5 event to the phone (0xff: none) */

void report_event_to_phone(int cmd);

#endif /* G1_COMM_BLE_REPORT_H_ */
