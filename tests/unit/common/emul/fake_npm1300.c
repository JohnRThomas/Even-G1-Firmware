#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <string.h>

#include "fake_npm1300.h"
#include "harness.h"

DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_read_burst, const struct device *, uint8_t, uint8_t,
		       void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_read, const struct device *, uint8_t, uint8_t,
		       uint8_t *);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_write, const struct device *, uint8_t, uint8_t,
		       uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_write2, const struct device *, uint8_t, uint8_t,
		       uint8_t, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_update, const struct device *, uint8_t, uint8_t,
		       uint8_t, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_set_timer, const struct device *, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reset, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_hibernate, const struct device *, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_add_callback, const struct device *,
		       struct gpio_callback *);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_remove_callback, const struct device *,
		       struct gpio_callback *);

static int read_burst_zero(const struct device *dev, uint8_t base, uint8_t offset, void *data,
			   size_t len)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(base);
	ARG_UNUSED(offset);
	memset(data, 0, len);
	return 0;
}

static int read_zero(const struct device *dev, uint8_t base, uint8_t offset, uint8_t *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(base);
	ARG_UNUSED(offset);
	*data = 0;
	return 0;
}

static void fake_npm1300_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(mfd_npm1300_reg_read_burst);
	RESET_FAKE(mfd_npm1300_reg_read);
	RESET_FAKE(mfd_npm1300_reg_write);
	RESET_FAKE(mfd_npm1300_reg_write2);
	RESET_FAKE(mfd_npm1300_reg_update);
	RESET_FAKE(mfd_npm1300_set_timer);
	RESET_FAKE(mfd_npm1300_reset);
	RESET_FAKE(mfd_npm1300_hibernate);
	RESET_FAKE(mfd_npm1300_add_callback);
	RESET_FAKE(mfd_npm1300_remove_callback);
	mfd_npm1300_reg_read_burst_fake.custom_fake = read_burst_zero;
	mfd_npm1300_reg_read_fake.custom_fake = read_zero;
}

G1_UNIT_RESET_HOOK(fake_npm1300_reset);
