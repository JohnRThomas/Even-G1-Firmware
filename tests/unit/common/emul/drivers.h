/*
 * Test API of the driver suites (DRIVERS in tests/unit/CMakeLists.txt): the firmware's devicetree
 * labels on emulated devices (common/boards/native_sim_drivers.overlay).
 */
#ifndef G1_UNIT_DRIVERS_H_
#define G1_UNIT_DRIVERS_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/fff.h>

#include "harness.h"

#define G1_I2C1 DEVICE_DT_GET(DT_NODELABEL(i2c1))

/* sets what device_is_ready() answers; every device is ready again after the test */
void g1_device_set_ready(const struct device *dev, bool ready);

/*
 * GPIO ports are never reset automatically: in before(), g1_gpio_remove_callback() each firmware
 * callback, then g1_gpio_port_reset() each port. gpio_emul fires a pin's callbacks when it is
 * configured.
 */
#define G1_GPIO0 DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define G1_GPIO1 DEVICE_DT_GET(DT_NODELABEL(gpio1))

/* interrupt bits as gpio_emul stores them */
#define G1_GPIO_INT_FLAGS                                                                          \
  (GPIO_INT_DISABLE | GPIO_INT_ENABLE | GPIO_INT_LEVELS_LOGICAL | GPIO_INT_EDGE |            \
   GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)

void g1_gpio_port_reset(const struct device *port);
/* safe on a never-initialised callback */
void g1_gpio_remove_callback(const struct device *port, struct gpio_callback *callback);
gpio_flags_t g1_gpio_flags(const struct device *port, gpio_pin_t pin);
/* one bit per pin */
gpio_port_value_t g1_gpio_outputs(const struct device *port);

/* nPM1300 MFD API fakes; reads return zeroes */
#define G1_NPM1300_DEV          DEVICE_DT_GET(DT_NODELABEL(pmic))
#define G1_NPM1300_LDSW_BASE    0x08
#define G1_NPM1300_LDSW1_EN_SET 0x00
#define G1_NPM1300_LDSW1_EN_CLR 0x01

DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_read, const struct device *, uint8_t, uint8_t,
      uint8_t *);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_reg_write, const struct device *, uint8_t, uint8_t,
      uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, mfd_npm1300_set_timer, const struct device *, uint32_t);

/*
 * Fake sensor drivers: a fetch latches the scripted values and channel_get reports the last
 * successful fetch, as the real drivers do. fail_*(ret, count) fails the next count calls with ret,
 * every call when count < 0.
 */
#define G1_FAKE_CHARGER_DEV DEVICE_DT_GET(DT_NODELABEL(pmic_charger))
#define G1_FAKE_IMU_DEV     DEVICE_DT_GET(DT_NODELABEL(lsm6dso))

/* charger: uV, uA (positive = discharge), milli-degrees C; fetched once the queue is used up */
void fake_charger_set(int64_t v_uv, int64_t i_ua, int64_t t_mdegc);
/* fetched first, in order; 0 or -ENOSPC */
int fake_charger_queue(int64_t v_uv, int64_t i_ua, int64_t t_mdegc);
void fake_charger_fail_fetch(int ret, int count);
int fake_charger_fetch_count(void);

/* IMU: accelerometer m/s^2, gyroscope rad/s; fake_imu_odr() is -1 until set */
#define FAKE_IMU_G 9.80665 /* standard gravity */

struct fake_imu_attr_call {
  enum sensor_channel chan;
  enum sensor_attribute attr;
  struct sensor_value val;
  int ret;
};

void fake_imu_set(double accel_x, double accel_y, double accel_z, double gyro_x, double gyro_y,
      double gyro_z);
int fake_imu_attr_count(void);
struct fake_imu_attr_call fake_imu_attr(int index);
int32_t fake_imu_odr(enum sensor_channel chan);
void fake_imu_fail_attr(enum sensor_channel chan, int ret, int count);
void fake_imu_fail_fetch(int ret, int count);
int fake_imu_fetch_count(void);
int fake_imu_get_count(void);

/*
 * mx25r64 as a RAM flash with the argument rules of nrf_qspi_nor.c (else -EINVAL). The reset erases
 * only what was touched, so g1_nor_mem() takes the length the test will use.
 * g1_nor_fail(G1_NOR_WRITE, count, 0) drops the data but reports success.
 */
#define G1_NOR_DEV    DEVICE_DT_GET(DT_NODELABEL(mx25r64))
#define G1_NOR_SIZE   DT_PROP(DT_NODELABEL(mx25r64), size)
#define G1_NOR_SECTOR 4096

enum g1_nor_op {
  G1_NOR_READ,
  G1_NOR_WRITE,
  G1_NOR_ERASE,
  G1_NOR_OPS
};

struct g1_nor_stats {
  int calls[G1_NOR_OPS];
  uint32_t last_addr[G1_NOR_OPS];
  size_t last_len[G1_NOR_OPS];
};

uint8_t *g1_nor_mem(uint32_t addr, size_t len);
void g1_nor_fail(enum g1_nor_op op, int count, int err);
const struct g1_nor_stats *g1_nor_stats(void);

#endif /* G1_UNIT_DRIVERS_H_ */
