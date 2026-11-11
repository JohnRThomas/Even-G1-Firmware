/* Weak BLE_DEBUG / ble_printk() for suites without ble_proto.c. */
#ifndef G1_UNIT_FAKES_LOG_H_
#define G1_UNIT_FAKES_LOG_H_

#include "harness.h"

extern int BLE_DEBUG;
void ble_printk(const char *fmt, ...);

extern struct g1_pkt_log g1_fake_ble_printk_log;

#endif /* G1_UNIT_FAKES_LOG_H_ */
