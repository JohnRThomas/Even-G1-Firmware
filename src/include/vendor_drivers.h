/*
 * Zephyr driver init functions the application calls directly.  Neither driver
 * declares them in a header.
 */
#ifndef G1_VENDOR_DRIVERS_H_
#define G1_VENDOR_DRIVERS_H_

#include <zephyr/device.h>

/* drivers/sensor/lsm6dso/lsm6dso.c: the driver's init function, for a sensor
 * that has just been powered.  Defined in that file by imu/lsm6dso_power_on_init.h,
 * which also keeps the driver from initialising the device at boot. */
int lsm6dso_power_on_init(const struct device *dev);

/* drivers/sensor/npm1300_charger/npm1300_charger.c (non-static in stock Zephyr) */
int npm1300_charger_init(const struct device *dev);

#endif /* G1_VENDOR_DRIVERS_H_ */
