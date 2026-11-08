/*
 * Changes how Zephyr's LSM6DSO driver starts, for an IMU that has no power at
 * boot (main() switches it on with power_for_imu_and_mic()).
 *
 * The top level CMakeLists.txt force-includes this header in front of the
 * SDK's drivers/sensor/lsm6dso/lsm6dso.c.  The SDK itself is not modified.
 *
 * - The device is defined without an init function, so the kernel marks it
 *   ready at boot without touching the bus.
 * - lsm6dso_power_on_init() runs the driver's init function; main() calls it
 *   once the sensor is powered.
 * - The driver logs at debug level (its accelerometer/gyroscope setup and every
 *   failed access).
 */
#ifndef IMU_LSM6DSO_POWER_ON_INIT_H_
#define IMU_LSM6DSO_POWER_ON_INIT_H_

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log_core.h>

#if DT_NUM_INST_STATUS_OKAY(st_lsm6dso) > 1
#error "lsm6dso_power_on_init() serves a single LSM6DSO instance"
#endif

#undef CONFIG_SENSOR_LOG_LEVEL
#define CONFIG_SENSOR_LOG_LEVEL LOG_LEVEL_DBG

int lsm6dso_power_on_init(const struct device *dev);

#undef SENSOR_DEVICE_DT_INST_DEFINE
#define SENSOR_DEVICE_DT_INST_DEFINE(inst, init_fn, ...)                        \
	int lsm6dso_power_on_init(const struct device *dev)                     \
	{                                                                       \
		return init_fn(dev);                                            \
	}                                                                       \
	SENSOR_DEVICE_DT_DEFINE(DT_DRV_INST(inst), NULL, __VA_ARGS__)

#endif /* IMU_LSM6DSO_POWER_ON_INIT_H_ */
