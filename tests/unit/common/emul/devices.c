#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>

#include "devices.h"
#include "harness.h"

#if defined(CONFIG_FLASH_SIMULATOR)
#include <zephyr/drivers/flash/flash_simulator.h>

/* flash.bin may survive from an earlier run */
static int soc_flash_boot_erase(void)
{
	size_t size;
	uint8_t *mem = flash_simulator_get_memory(DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller)),
						  &size);

	memset(mem, 0xff, size);
	return 0;
}

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY < 99, "soc_flash_boot_erase runs after flash init");
SYS_INIT(soc_flash_boot_erase, POST_KERNEL, 99);
#endif

static int g1_stub_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define G1_STUB_DEVICE(node_id)                                                                    \
	DEVICE_DT_DEFINE(node_id, g1_stub_init, NULL, NULL, NULL, POST_KERNEL,                     \
			 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_FOREACH_STATUS_OKAY(vnd_g1_unit_i2c_chip, G1_STUB_DEVICE)
DT_FOREACH_STATUS_OKAY(vnd_g1_unit_npm1300, G1_STUB_DEVICE)

#define G1_NPM1300_OUTPUT(node_id)                                                                 \
	static const struct g1_npm1300_regulator_config _CONCAT(g1_npm1300_cfg_, node_id) = {     \
		.common = {                                                                        \
			.min_uv = DT_PROP_OR(node_id, regulator_min_microvolt, INT32_MIN),         \
			.max_uv = DT_PROP_OR(node_id, regulator_max_microvolt, INT32_MAX),         \
			.init_uv = DT_PROP_OR(node_id, regulator_init_microvolt, INT32_MIN),       \
		},                                                                                 \
		.mfd = DEVICE_DT_GET(DT_PARENT(DT_PARENT(node_id))),                               \
	};                                                                                         \
	DEVICE_DT_DEFINE(node_id, g1_stub_init, NULL, NULL, &_CONCAT(g1_npm1300_cfg_, node_id),   \
			 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

#define G1_NPM1300_REGULATORS(node_id)                                                             \
	G1_STUB_DEVICE(node_id)                                                                    \
	DT_FOREACH_CHILD_STATUS_OKAY(node_id, G1_NPM1300_OUTPUT)

DT_FOREACH_STATUS_OKAY(vnd_g1_unit_npm1300_regulator, G1_NPM1300_REGULATORS)

#define G1_NOT_READY_MAX 8

static const struct device *not_ready[G1_NOT_READY_MAX];

void g1_device_set_ready(const struct device *dev, bool ready)
{
	/* device_is_ready() checks init_res */
	dev->state->init_res = ready ? 0 : (uint8_t)EIO;
	for (int i = 0; i < G1_NOT_READY_MAX; i++) {
		if (not_ready[i] == dev) {
			if (ready) {
				not_ready[i] = NULL;
			}
			return;
		}
	}
	if (!ready) {
		for (int i = 0; i < G1_NOT_READY_MAX; i++) {
			if (not_ready[i] == NULL) {
				not_ready[i] = dev;
				return;
			}
		}
		__ASSERT(false, "g1_device_set_ready: more than %d devices not ready", G1_NOT_READY_MAX);
	}
}

static void devices_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	for (int i = 0; i < G1_NOT_READY_MAX; i++) {
		if (not_ready[i]) {
			not_ready[i]->state->init_res = 0;
			not_ready[i] = NULL;
		}
	}
}

G1_UNIT_RESET_HOOK(devices_reset);
