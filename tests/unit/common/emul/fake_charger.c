#define DT_DRV_COMPAT vnd_g1_unit_charger

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include "fake_charger.h"
#include "harness.h"

struct fake_charger_config {
	int32_t current_microamp;
};

static struct {
	struct fake_charger_sample sample;  /* fallback when the queue is empty */
	struct fake_charger_sample latched;
	struct fake_charger_sample queue[FAKE_CHARGER_QUEUE_MAX];
	int q_head;
	int q_len;
	int fail_ret;
	int fail_left;
	int fetches;
	int gets;
	enum sensor_channel get_chan[FAKE_CHARGER_GET_LOG];
} fc;

static struct sensor_value micro_to_value(int64_t micro)
{
	/* C truncation: val2 has the value's sign */
	struct sensor_value v = {
		.val1 = (int32_t)(micro / 1000000),
		.val2 = (int32_t)(micro % 1000000),
	};

	return v;
}

struct fake_charger_sample fake_charger_sample(int64_t v_uv, int64_t i_ua, int64_t t_mdegc)
{
	struct fake_charger_sample s = {
		.voltage = micro_to_value(v_uv),
		.current = micro_to_value(i_ua),
		.temp = micro_to_value(t_mdegc * 1000),
	};

	return s;
}

void fake_charger_set_sample(const struct fake_charger_sample *s)
{
	fc.sample = *s;
}

int fake_charger_queue_sample(const struct fake_charger_sample *s)
{
	if (fc.q_len >= FAKE_CHARGER_QUEUE_MAX) {
		return -ENOSPC;
	}
	fc.queue[(fc.q_head + fc.q_len) % FAKE_CHARGER_QUEUE_MAX] = *s;
	fc.q_len++;
	return 0;
}

int fake_charger_queued(void)
{
	return fc.q_len;
}

void fake_charger_fail_fetch(int ret, int n)
{
	fc.fail_ret = ret;
	fc.fail_left = n;
}

int fake_charger_fetch_count(void)
{
	return fc.fetches;
}

int fake_charger_get_count(void)
{
	return fc.gets;
}

enum sensor_channel fake_charger_get_chan(int i)
{
	__ASSERT(i >= 0 && i < MIN(fc.gets, FAKE_CHARGER_GET_LOG), "get %d not logged", i);
	return fc.get_chan[i];
}

static int fake_charger_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	ARG_UNUSED(chan);
	fc.fetches++;
	if (!device_is_ready(dev)) {
		return -EIO;
	}
	if (fc.fail_left != 0) {
		if (fc.fail_left > 0) {
			fc.fail_left--;
		}
		return fc.fail_ret;
	}
	if (fc.q_len > 0) {
		fc.latched = fc.queue[fc.q_head];
		fc.q_head = (fc.q_head + 1) % FAKE_CHARGER_QUEUE_MAX;
		fc.q_len--;
	} else {
		fc.latched = fc.sample;
	}
	return 0;
}

static int fake_charger_channel_get(const struct device *dev, enum sensor_channel chan,
				    struct sensor_value *val)
{
	const struct fake_charger_config *cfg = dev->config;

	if (fc.gets < FAKE_CHARGER_GET_LOG) {
		fc.get_chan[fc.gets] = chan;
	}
	fc.gets++;

	switch ((uint32_t)chan) {
	case SENSOR_CHAN_GAUGE_VOLTAGE:
		*val = fc.latched.voltage;
		break;
	case SENSOR_CHAN_GAUGE_AVG_CURRENT:
		*val = fc.latched.current;
		break;
	case SENSOR_CHAN_GAUGE_TEMP:
		*val = fc.latched.temp;
		break;
	case SENSOR_CHAN_NPM1300_CHARGER_STATUS:
		val->val1 = fc.latched.status;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_NPM1300_CHARGER_ERROR:
		val->val1 = fc.latched.error;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT:
		*val = micro_to_value(cfg->current_microamp);
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static const struct sensor_driver_api fake_charger_api = {
	.sample_fetch = fake_charger_sample_fetch,
	.channel_get = fake_charger_channel_get,
};

static int fake_charger_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define FAKE_CHARGER_DEFINE(n)                                                                     \
	static const struct fake_charger_config fake_charger_cfg_##n = {                           \
		.current_microamp = DT_INST_PROP(n, current_microamp),                             \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, fake_charger_init, NULL, NULL, &fake_charger_cfg_##n,             \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_charger_api);

DT_INST_FOREACH_STATUS_OKAY(FAKE_CHARGER_DEFINE)

static void fake_charger_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	memset(&fc, 0, sizeof(fc));
}

G1_UNIT_RESET_HOOK(fake_charger_reset);
