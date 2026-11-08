/*
 * Declarations of pt_data.c.
 */
#ifndef G1_SERVICES_PT_PT_DATA_H_
#define G1_SERVICES_PT_PT_DATA_H_

#include "services/production_test/production_test.h"

int init_nfc_comm(void); /* binds the NFC tag mailbox as the production-test channel */
int setUARTDevice(const struct device *dev);
int setNFCDevice(void *nfc);
int lock_sensor_mutex(void);
int unlock_sensor_mutex(void);
int process_pt_data(void *arg);

#endif /* G1_SERVICES_PT_PT_DATA_H_ */
