/*
 * What the application takes from drivers directly; no Zephyr header declares it.
 */
#ifndef G1_VENDOR_DRIVERS_H_
#define G1_VENDOR_DRIVERS_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>

/* drivers/sensor/npm1300_charger/npm1300_charger.c */
int npm1300_charger_init(const struct device *dev);

/* Copy of the private config struct of drivers/regulator/regulator_npm1300.c */
struct regulator_npm1300_config {
  struct regulator_common_config common;
  const struct device *mfd;
  uint8_t source;
  int32_t retention_uv;
  struct gpio_dt_spec enable_gpios;
  struct gpio_dt_spec retention_gpios;
  struct gpio_dt_spec pwm_gpios;
};

/* the nPM1300 multi-function device behind one of its regulators */
#define NPM_MFD(dev) (((const struct regulator_npm1300_config *)(dev)->config)->mfd)

#endif /* G1_VENDOR_DRIVERS_H_ */
