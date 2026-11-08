/*
 * Entry point of Zephyr's nPM1300 charger driver that the application calls
 * directly; no SDK header declares it.
 */
#ifndef G1_DRIVERS_PMIC_NPM1300_CHARGER_H_
#define G1_DRIVERS_PMIC_NPM1300_CHARGER_H_

#include <zephyr/device.h>

/* drivers/sensor/npm1300_charger/npm1300_charger.c */
int npm1300_charger_init(const struct device *dev);

#endif /* G1_DRIVERS_PMIC_NPM1300_CHARGER_H_ */
