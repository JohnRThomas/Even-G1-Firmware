/*
 * Fake sensor drivers for the nodes the firmware reads through the sensor API: the nPM1300 charger
 * (pmic_charger) and the LSM6DSO IMU (lsm6dso). Only the calls and channels the firmware uses.
 */
#include <errno.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>

#include "drivers.h"

#define CHARGER_QUEUE_MAX 32
#define IMU_ATTR_LOG      64

/* V, A (positive = discharge), degrees C */
struct charger_reading {
  struct sensor_value voltage;
  struct sensor_value current;
  struct sensor_value temp;
};

static struct {
  struct charger_reading set;
  struct charger_reading queue[CHARGER_QUEUE_MAX];
  int queued;
  int queue_next;
  struct charger_reading latched;
  int fail_ret;
  int fail_count;
  int fetches;
} charger;

struct imu_reading {
  struct sensor_value accel[3];
  struct sensor_value gyro[3];
};

static struct {
  struct imu_reading set;
  struct imu_reading latched;
  int attr_calls;
  struct fake_imu_attr_call attr_log[IMU_ATTR_LOG];
  int32_t odr_accel;
  int32_t odr_gyro;
  enum sensor_channel attr_fail_chan; /* SENSOR_CHAN_ALL: any */
  int attr_fail_ret;
  int attr_fail_count;
  int fetch_fail_ret;
  int fetch_fail_count;
  int fetches;
  int gets;
} imu;

/* uses up one of count failures; count < 0 never runs out */
static bool take_fault(int *count)
{
  if (*count == 0) {
    return false;
  }
  if (*count > 0) {
    (*count)--;
  }
  return true;
}

static struct sensor_value micro_to_value(int64_t micro)
{
  /* C truncation: val2 has the value's sign */
  struct sensor_value value = {
    .val1 = (int32_t)(micro / 1000000),
    .val2 = (int32_t)(micro % 1000000),
  };

  return value;
}

static struct charger_reading to_reading(int64_t v_uv, int64_t i_ua, int64_t t_mdegc)
{
  struct charger_reading reading = {
    .voltage = micro_to_value(v_uv),
    .current = micro_to_value(i_ua),
    .temp = micro_to_value(t_mdegc * 1000),
  };

  return reading;
}

void fake_charger_set(int64_t v_uv, int64_t i_ua, int64_t t_mdegc)
{
  charger.set = to_reading(v_uv, i_ua, t_mdegc);
}

int fake_charger_queue(int64_t v_uv, int64_t i_ua, int64_t t_mdegc)
{
  if (charger.queued >= CHARGER_QUEUE_MAX) {
    return -ENOSPC;
  }
  charger.queue[charger.queued++] = to_reading(v_uv, i_ua, t_mdegc);
  return 0;
}

void fake_charger_fail_fetch(int ret, int count)
{
  charger.fail_ret = ret;
  charger.fail_count = count;
}

int fake_charger_fetch_count(void)
{
  return charger.fetches;
}

static int charger_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
  ARG_UNUSED(chan);
  charger.fetches++;
  if (!device_is_ready(dev)) {
    return -EIO;
  }
  if (take_fault(&charger.fail_count)) {
    return charger.fail_ret;
  }
  if (charger.queue_next < charger.queued) {
    charger.latched = charger.queue[charger.queue_next++];
  } else {
    charger.latched = charger.set;
  }
  return 0;
}

static int charger_channel_get(const struct device *dev, enum sensor_channel chan,
             struct sensor_value *val)
{
  ARG_UNUSED(dev);
  switch ((uint32_t)chan) {
  case SENSOR_CHAN_GAUGE_VOLTAGE:
    *val = charger.latched.voltage;
    break;
  case SENSOR_CHAN_GAUGE_AVG_CURRENT:
    *val = charger.latched.current;
    break;
  case SENSOR_CHAN_GAUGE_TEMP:
    *val = charger.latched.temp;
    break;
  /* BCHGCHARGESTATUS and BCHGERRREASON, which no test scripts */
  case SENSOR_CHAN_NPM1300_CHARGER_STATUS:
  case SENSOR_CHAN_NPM1300_CHARGER_ERROR:
    val->val1 = 0;
    val->val2 = 0;
    break;
  /* the charge current, as the real driver reports it */
  case SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT:
    *val = micro_to_value(DT_PROP(DT_NODELABEL(pmic_charger), current_microamp));
    break;
  default:
    return -ENOTSUP;
  }
  return 0;
}

static int imu_attr_set(const struct device *dev, enum sensor_channel chan,
      enum sensor_attribute attr, const struct sensor_value *val)
{
  int ret = 0;

  if (!device_is_ready(dev)) {
    ret = -EIO;
  } else if ((imu.attr_fail_chan == SENSOR_CHAN_ALL || imu.attr_fail_chan == chan) &&
       take_fault(&imu.attr_fail_count)) {
    ret = imu.attr_fail_ret;
  }
  if (imu.attr_calls < IMU_ATTR_LOG) {
    struct fake_imu_attr_call *call = &imu.attr_log[imu.attr_calls];

    call->chan = chan;
    call->attr = attr;
    call->val = *val;
    call->ret = ret;
  }
  imu.attr_calls++;
  if (ret == 0 && attr == SENSOR_ATTR_SAMPLING_FREQUENCY) {
    if (chan == SENSOR_CHAN_ACCEL_XYZ) {
      imu.odr_accel = val->val1;
    } else if (chan == SENSOR_CHAN_GYRO_XYZ) {
      imu.odr_gyro = val->val1;
    }
  }
  return ret;
}

static int imu_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
  ARG_UNUSED(chan);
  imu.fetches++;
  if (!device_is_ready(dev)) {
    return -EIO;
  }
  if (take_fault(&imu.fetch_fail_count)) {
    return imu.fetch_fail_ret;
  }
  imu.latched = imu.set;
  return 0;
}

static int imu_channel_get(const struct device *dev, enum sensor_channel chan,
         struct sensor_value *val)
{
  ARG_UNUSED(dev);
  imu.gets++;
  switch (chan) {
  case SENSOR_CHAN_ACCEL_XYZ:
    memcpy(val, imu.latched.accel, sizeof(imu.latched.accel));
    return 0;
  case SENSOR_CHAN_GYRO_XYZ:
    memcpy(val, imu.latched.gyro, sizeof(imu.latched.gyro));
    return 0;
  case SENSOR_CHAN_ACCEL_X:
  case SENSOR_CHAN_ACCEL_Y:
  case SENSOR_CHAN_ACCEL_Z:
    *val = imu.latched.accel[chan - SENSOR_CHAN_ACCEL_X];
    return 0;
  case SENSOR_CHAN_GYRO_X:
  case SENSOR_CHAN_GYRO_Y:
  case SENSOR_CHAN_GYRO_Z:
    *val = imu.latched.gyro[chan - SENSOR_CHAN_GYRO_X];
    return 0;
  default:
    return -ENOTSUP;
  }
}

static const struct sensor_driver_api charger_api = {
  .sample_fetch = charger_sample_fetch,
  .channel_get = charger_channel_get,
};

static const struct sensor_driver_api imu_api = {
  .attr_set = imu_attr_set,
  .sample_fetch = imu_sample_fetch,
  .channel_get = imu_channel_get,
};

DEVICE_DT_DEFINE(DT_NODELABEL(pmic_charger), NULL, NULL, NULL, NULL, POST_KERNEL,
     CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &charger_api);
DEVICE_DT_DEFINE(DT_NODELABEL(lsm6dso), NULL, NULL, NULL, NULL, POST_KERNEL,
     CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &imu_api);

void fake_imu_set(double accel_x, double accel_y, double accel_z, double gyro_x, double gyro_y,
      double gyro_z)
{
  sensor_value_from_double(&imu.set.accel[0], accel_x);
  sensor_value_from_double(&imu.set.accel[1], accel_y);
  sensor_value_from_double(&imu.set.accel[2], accel_z);
  sensor_value_from_double(&imu.set.gyro[0], gyro_x);
  sensor_value_from_double(&imu.set.gyro[1], gyro_y);
  sensor_value_from_double(&imu.set.gyro[2], gyro_z);
}

int fake_imu_attr_count(void)
{
  return imu.attr_calls;
}

struct fake_imu_attr_call fake_imu_attr(int index)
{
  __ASSERT(index >= 0 && index < MIN(imu.attr_calls, IMU_ATTR_LOG),
     "attr_set call %d not logged", index);
  return imu.attr_log[index];
}

int32_t fake_imu_odr(enum sensor_channel chan)
{
  return chan == SENSOR_CHAN_ACCEL_XYZ ? imu.odr_accel
         : chan == SENSOR_CHAN_GYRO_XYZ ? imu.odr_gyro
               : -1;
}

void fake_imu_fail_attr(enum sensor_channel chan, int ret, int count)
{
  imu.attr_fail_chan = chan;
  imu.attr_fail_ret = ret;
  imu.attr_fail_count = count;
}

void fake_imu_fail_fetch(int ret, int count)
{
  imu.fetch_fail_ret = ret;
  imu.fetch_fail_count = count;
}

int fake_imu_fetch_count(void)
{
  return imu.fetches;
}

int fake_imu_get_count(void)
{
  return imu.gets;
}

static void fake_sensors_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);
  memset(&charger, 0, sizeof(charger));
  memset(&imu, 0, sizeof(imu));
  imu.odr_accel = -1;
  imu.odr_gyro = -1;
}

G1_UNIT_RESET_HOOK(fake_sensors_reset);
