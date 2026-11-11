#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "gpio_util.h"

void g1_gpio_port_reset(const struct device *port)
{
	const struct gpio_driver_config *cfg = port->config;
	int ret;

	for (gpio_pin_t pin = 0; pin < 32; pin++) {
		if (!(cfg->port_pin_mask & BIT(pin))) {
			continue;
		}
		ret = gpio_pin_interrupt_configure(port, pin, GPIO_INT_DISABLE);
		__ASSERT(ret == 0, "pin %u: interrupt disable %d", pin, ret);
		/* output latch 0 */
		ret = gpio_pin_configure(port, pin, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
		__ASSERT(ret == 0, "pin %u: output low %d", pin, ret);
		/* input level 0 */
		ret = gpio_pin_configure(port, pin, GPIO_INPUT | GPIO_PULL_DOWN);
		__ASSERT(ret == 0, "pin %u: pull-down %d", pin, ret);
		ret = gpio_pin_configure(port, pin, GPIO_DISCONNECTED);
		__ASSERT(ret == 0, "pin %u: disconnect %d", pin, ret);
	}
}

void g1_gpio_remove_callback(const struct device *port, struct gpio_callback *cb)
{
	/* asserts on a NULL handler */
	if (cb->handler != NULL) {
		(void)gpio_remove_callback(port, cb);
	}
}

gpio_flags_t g1_gpio_flags(const struct device *port, gpio_pin_t pin)
{
	gpio_flags_t flags = 0;

	zassert_ok(gpio_emul_flags_get(port, pin, &flags), "gpio_emul_flags_get(%s, %u)",
		   port->name, pin);
	return flags;
}

gpio_port_value_t g1_gpio_outputs(const struct device *port)
{
	const struct gpio_driver_config *cfg = port->config;
	gpio_port_value_t values = 0;

	zassert_ok(gpio_emul_output_get_masked(port, cfg->port_pin_mask, &values),
		   "gpio_emul_output_get_masked(%s)", port->name);
	return values;
}
