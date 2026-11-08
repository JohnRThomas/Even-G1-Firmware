/*
 * Driver functions the application calls directly; no Zephyr header declares them.
 */
#ifndef G1_VENDOR_DRIVERS_H_
#define G1_VENDOR_DRIVERS_H_

#include <zephyr/device.h>

/* drivers/sensor/npm1300_charger/npm1300_charger.c */
int npm1300_charger_init(const struct device *dev);

#endif /* G1_VENDOR_DRIVERS_H_ */
