/* g1_npm1300_regulator_config mirrors Zephyr's private regulator_npm1300_config. */
#ifndef G1_UNIT_EMUL_DEVICES_H_
#define G1_UNIT_EMUL_DEVICES_H_

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>

struct g1_npm1300_regulator_config {
	struct regulator_common_config common;
	const struct device *mfd;
	uint8_t source;
	int32_t retention_uv;
	struct gpio_dt_spec enable_gpios;
	struct gpio_dt_spec retention_gpios;
	struct gpio_dt_spec pwm_gpios;
};

void g1_device_set_ready(const struct device *dev, bool ready);

#endif /* G1_UNIT_EMUL_DEVICES_H_ */
