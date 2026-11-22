/*
 * The driver suites' devices without an emulator or a fake driver, g1_device_set_ready(), the GPIO
 * helpers and the nPM1300 MFD fakes.
 */
#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>
#include <zephyr/device.h> /* flash_simulator.h needs it first */
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/drivers/regulator.h>

#include "drivers.h"

/* flash.bin may survive from an earlier run */
static int soc_flash_boot_erase(void) {
  size_t size;
  uint8_t *mem = flash_simulator_get_memory(DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller)), &size);

  memset(mem, 0xff, size);
  return 0;
}

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY < 99, "soc_flash_boot_erase runs after flash init");
SYS_INIT(soc_flash_boot_erase, POST_KERNEL, 99);

/* i2c_emul needs a device for every chip on its bus; a chip without an emulator NAKs */
#define G1_STUB_DEVICE(node_id)                                                                                        \
  DEVICE_DT_DEFINE(node_id, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_FOREACH_STATUS_OKAY(vnd_g1_unit_i2c_chip, G1_STUB_DEVICE)

/*
 * opt3007.c and charger_box.c take the PMIC from an LDO's config, which they read as the private
 * struct regulator_npm1300_config of Zephyr's regulator_npm1300.c. It starts with these fields.
 */
struct npm1300_regulator_config {
  struct regulator_common_config common;
  const struct device *mfd;
};

static const struct npm1300_regulator_config ldo_config = {
  .mfd = G1_NPM1300_DEV,
};

DEVICE_DT_DEFINE(DT_NODELABEL(pmic_ldo1), NULL, NULL, NULL, &ldo_config, POST_KERNEL,
                 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
DEVICE_DT_DEFINE(DT_NODELABEL(pmic_ldo2), NULL, NULL, NULL, &ldo_config, POST_KERNEL,
                 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

/* the devices a test made not ready, once per call */
#define NOT_READY_MAX 16

static const struct device *not_ready[NOT_READY_MAX];
static int not_ready_count;

void g1_device_set_ready(const struct device *dev, bool ready) {
  /* device_is_ready() checks init_res */
  dev->state->init_res = ready ? 0 : (uint8_t)EIO;
  if (!ready) {
    __ASSERT(not_ready_count < NOT_READY_MAX, "more than %d devices made not ready", NOT_READY_MAX);
    not_ready[not_ready_count++] = dev;
  }
}

void g1_gpio_port_reset(const struct device *port) {
  const struct gpio_driver_config *config = port->config;
  int ret;

  for (gpio_pin_t pin = 0; pin < 32; pin++) {
    if (!(config->port_pin_mask & BIT(pin))) {
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

void g1_gpio_remove_callback(const struct device *port, struct gpio_callback *callback) {
  /* asserts on a NULL handler */
  if (callback->handler != NULL) {
    (void)gpio_remove_callback(port, callback);
  }
}

gpio_flags_t g1_gpio_flags(const struct device *port, gpio_pin_t pin) {
  gpio_flags_t flags = 0;

  zassert_ok(gpio_emul_flags_get(port, pin, &flags), "gpio_emul_flags_get(%s, %u)", port->name, pin);
  return flags;
}

gpio_port_value_t g1_gpio_outputs(const struct device *port) {
  const struct gpio_driver_config *config = port->config;
  gpio_port_value_t values = 0;

  zassert_ok(gpio_emul_output_get_masked(port, config->port_pin_mask, &values), "gpio_emul_output_get_masked(%s)",
             port->name);
  return values;
}

DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_read, const struct device *, uint8_t, uint8_t, uint8_t *);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_write, const struct device *, uint8_t, uint8_t, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, mfd_npm1300_set_timer, const struct device *, uint32_t);

static int read_zero(const struct device *dev, uint8_t base, uint8_t offset, uint8_t *data) {
  ARG_UNUSED(dev);
  ARG_UNUSED(base);
  ARG_UNUSED(offset);
  *data = 0;
  return 0;
}

static void devices_reset(bool keep_media) {
  ARG_UNUSED(keep_media);
  for (int index = 0; index < not_ready_count; index++) {
    not_ready[index]->state->init_res = 0;
  }
  not_ready_count = 0;
  RESET_FAKE(mfd_npm1300_reg_read);
  RESET_FAKE(mfd_npm1300_reg_write);
  RESET_FAKE(mfd_npm1300_set_timer);
  mfd_npm1300_reg_read_fake.custom_fake = read_zero;
}

G1_UNIT_RESET_HOOK(devices_reset);
