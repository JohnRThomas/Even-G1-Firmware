/*
 * Battery state of charge (fuel_gauge.c).
 */
#ifndef G1_SERVICES_POWER_FUEL_GAUGE_H_
#define G1_SERVICES_POWER_FUEL_GAUGE_H_

#include <stdbool.h>
#include <zephyr/device.h>

int fuel_gauge_init(const struct device *charger);
bool is_old_board(void);
int fuel_gauge_update(const struct device *charger);

#endif /* G1_SERVICES_POWER_FUEL_GAUGE_H_ */
