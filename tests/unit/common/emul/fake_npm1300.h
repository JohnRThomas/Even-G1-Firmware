/* nPM1300 MFD API fakes; reads return zeroes. */
#ifndef G1_UNIT_FAKE_NPM1300_H_
#define G1_UNIT_FAKE_NPM1300_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/fff.h>

#define G1_NPM1300_DEV DEVICE_DT_GET(DT_NODELABEL(pmic))

#define G1_NPM1300_LDSW_BASE       0x08
#define G1_NPM1300_LDSW1_EN_SET    0x00
#define G1_NPM1300_LDSW1_EN_CLR    0x01

DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_read_burst, const struct device *, uint8_t, uint8_t,
			void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_read, const struct device *, uint8_t, uint8_t,
			uint8_t *);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_write, const struct device *, uint8_t, uint8_t,
			uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_write2, const struct device *, uint8_t, uint8_t,
			uint8_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_update, const struct device *, uint8_t, uint8_t,
			uint8_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_set_timer, const struct device *, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reset, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_hibernate, const struct device *, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_add_callback, const struct device *,
			struct gpio_callback *);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_remove_callback, const struct device *,
			struct gpio_callback *);

#endif /* G1_UNIT_FAKE_NPM1300_H_ */
