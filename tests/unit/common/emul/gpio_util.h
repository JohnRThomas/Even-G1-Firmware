/*
 * Ports are never reset automatically: in before(), g1_gpio_remove_callback() each firmware
 * callback, then g1_gpio_port_reset() each port. gpio_emul fires a pin's callbacks when it is
 * configured.
 */
#ifndef G1_UNIT_GPIO_UTIL_H_
#define G1_UNIT_GPIO_UTIL_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#define G1_GPIO0 DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define G1_GPIO1 DEVICE_DT_GET(DT_NODELABEL(gpio1))

/* interrupt bits as gpio_emul stores them */
#define G1_GPIO_INT_FLAGS                                                                          \
	(GPIO_INT_DISABLE | GPIO_INT_ENABLE | GPIO_INT_LEVELS_LOGICAL | GPIO_INT_EDGE |            \
	 GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)

void g1_gpio_port_reset(const struct device *port);

/* safe on a never-initialised cb */
void g1_gpio_remove_callback(const struct device *port, struct gpio_callback *cb);

gpio_flags_t g1_gpio_flags(const struct device *port, gpio_pin_t pin);

/* bit n = pin n */
gpio_port_value_t g1_gpio_outputs(const struct device *port);

#endif /* G1_UNIT_GPIO_UTIL_H_ */
