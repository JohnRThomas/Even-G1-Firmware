/* Sensor API subset used by imu_fusion.c. */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include "fake_imu.h"
#include "harness.h"

struct fake_imu_data {
	struct fake_imu_sample idle;     /* fallback when the queue is empty */
	struct fake_imu_sample queue[FAKE_IMU_QUEUE_MAX];
	int q_head;
	int q_count;
	struct fake_imu_sample latched;

	int attr_calls;
	struct fake_imu_attr_call attr_log[FAKE_IMU_ATTR_LOG];
	int32_t odr_accel;
	int32_t odr_gyro;

	enum sensor_channel attr_fail_chan; /* SENSOR_CHAN_ALL: any */
	int attr_fail_ret;
	int attr_fail_n;
	int fetch_fail_ret;
	int fetch_fail_n;

	int fetches;
	int gets;
};

static struct fake_imu_data fake_imu;

static bool take_fault(int *n)
{
	if (*n == 0) {
		return false;
	}
	if (*n > 0) {
		(*n)--;
	}
	return true;
}

static int fake_imu_attr_set(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, const struct sensor_value *val)
{
	struct fake_imu_data *d = dev->data;
	int ret = 0;

	if (!device_is_ready(dev)) {
		ret = -EIO;
	} else if ((d->attr_fail_chan == SENSOR_CHAN_ALL || d->attr_fail_chan == chan) &&
		   take_fault(&d->attr_fail_n)) {
		ret = d->attr_fail_ret;
	}
	if (d->attr_calls < FAKE_IMU_ATTR_LOG) {
		struct fake_imu_attr_call *c = &d->attr_log[d->attr_calls];

		c->chan = chan;
		c->attr = attr;
		c->val = *val;
		c->ret = ret;
	}
	d->attr_calls++;
	if (ret == 0 && attr == SENSOR_ATTR_SAMPLING_FREQUENCY) {
		if (chan == SENSOR_CHAN_ACCEL_XYZ) {
			d->odr_accel = val->val1;
		} else if (chan == SENSOR_CHAN_GYRO_XYZ) {
			d->odr_gyro = val->val1;
		}
	}
	return ret;
}

static int fake_imu_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct fake_imu_data *d = dev->data;

	ARG_UNUSED(chan);
	d->fetches++;
	if (!device_is_ready(dev)) {
		return -EIO;
	}
	if (take_fault(&d->fetch_fail_n)) {
		return d->fetch_fail_ret;
	}
	if (d->q_count > 0) {
		d->latched = d->queue[d->q_head];
		d->q_head = (d->q_head + 1) % FAKE_IMU_QUEUE_MAX;
		d->q_count--;
	} else {
		d->latched = d->idle;
	}
	return 0;
}

static int fake_imu_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	struct fake_imu_data *d = dev->data;

	d->gets++;
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		memcpy(val, d->latched.accel, sizeof(d->latched.accel));
		return 0;
	case SENSOR_CHAN_GYRO_XYZ:
		memcpy(val, d->latched.gyro, sizeof(d->latched.gyro));
		return 0;
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		*val = d->latched.accel[chan - SENSOR_CHAN_ACCEL_X];
		return 0;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		*val = d->latched.gyro[chan - SENSOR_CHAN_GYRO_X];
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct sensor_driver_api fake_imu_api = {
	.attr_set = fake_imu_attr_set,
	.sample_fetch = fake_imu_sample_fetch,
	.channel_get = fake_imu_channel_get,
};

static int fake_imu_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_DEFINE(DT_NODELABEL(lsm6dso), fake_imu_init, NULL, &fake_imu, NULL, POST_KERNEL,
		 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_imu_api);

struct fake_imu_sample fake_imu_sample(double ax, double ay, double az, double gx, double gy,
				       double gz)
{
	struct fake_imu_sample s;

	sensor_value_from_double(&s.accel[0], ax);
	sensor_value_from_double(&s.accel[1], ay);
	sensor_value_from_double(&s.accel[2], az);
	sensor_value_from_double(&s.gyro[0], gx);
	sensor_value_from_double(&s.gyro[1], gy);
	sensor_value_from_double(&s.gyro[2], gz);
	return s;
}

void fake_imu_set_sample(const struct fake_imu_sample *s)
{
	fake_imu.idle = *s;
}

int fake_imu_queue_sample(const struct fake_imu_sample *s)
{
	if (fake_imu.q_count >= FAKE_IMU_QUEUE_MAX) {
		return -ENOSPC;
	}
	fake_imu.queue[(fake_imu.q_head + fake_imu.q_count) % FAKE_IMU_QUEUE_MAX] = *s;
	fake_imu.q_count++;
	return 0;
}

int fake_imu_queued(void)
{
	return fake_imu.q_count;
}

int fake_imu_attr_count(void)
{
	return fake_imu.attr_calls;
}

struct fake_imu_attr_call fake_imu_attr(int i)
{
	__ASSERT(i >= 0 && i < MIN(fake_imu.attr_calls, FAKE_IMU_ATTR_LOG),
		 "attr_set call %d not logged", i);
	return fake_imu.attr_log[i];
}

int32_t fake_imu_odr(enum sensor_channel chan)
{
	return chan == SENSOR_CHAN_ACCEL_XYZ ? fake_imu.odr_accel
	       : chan == SENSOR_CHAN_GYRO_XYZ ? fake_imu.odr_gyro
					     : -1;
}

void fake_imu_fail_attr(enum sensor_channel chan, int ret, int n)
{
	fake_imu.attr_fail_chan = chan;
	fake_imu.attr_fail_ret = ret;
	fake_imu.attr_fail_n = n;
}

void fake_imu_fail_fetch(int ret, int n)
{
	fake_imu.fetch_fail_ret = ret;
	fake_imu.fetch_fail_n = n;
}

int fake_imu_fetch_count(void)
{
	return fake_imu.fetches;
}

int fake_imu_get_count(void)
{
	return fake_imu.gets;
}

static void fake_imu_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	memset(&fake_imu, 0, sizeof(fake_imu));
	fake_imu.odr_accel = -1;
	fake_imu.odr_gyro = -1;
}

G1_UNIT_RESET_HOOK(fake_imu_reset);
