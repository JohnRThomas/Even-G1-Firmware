/*
 * Fake lsm6dso sensor driver: each fetch latches the oldest queued sample, else the set one (accel
 * m/s^2, gyro rad/s). fail_* with n < 0 fails every call; fake_imu_odr() is -1 until set.
 */
#ifndef G1_UNIT_FAKE_IMU_H_
#define G1_UNIT_FAKE_IMU_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#define G1_FAKE_IMU_DEV DEVICE_DT_GET(DT_NODELABEL(lsm6dso))

#define FAKE_IMU_QUEUE_MAX 1024
#define FAKE_IMU_ATTR_LOG  64

/* standard gravity */
#define FAKE_IMU_G 9.80665

struct fake_imu_sample {
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
};

struct fake_imu_attr_call {
	enum sensor_channel chan;
	enum sensor_attribute attr;
	struct sensor_value val;
	int ret;
};

struct fake_imu_sample fake_imu_sample(double ax, double ay, double az, double gx, double gy,
				       double gz);
void fake_imu_set_sample(const struct fake_imu_sample *s);
int fake_imu_queue_sample(const struct fake_imu_sample *s);
int fake_imu_queued(void);

int fake_imu_attr_count(void);
struct fake_imu_attr_call fake_imu_attr(int i);
int32_t fake_imu_odr(enum sensor_channel chan);

void fake_imu_fail_attr(enum sensor_channel chan, int ret, int n);
void fake_imu_fail_fetch(int ret, int n);

int fake_imu_fetch_count(void);
int fake_imu_get_count(void);

#endif /* G1_UNIT_FAKE_IMU_H_ */
