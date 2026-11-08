/*
 * nPM1300 PMIC: the register sequences behind the power rails (panel LDSW1 /
 * BUCK2, IMU and microphone LDSW2), ship mode, the charger (enable, termination
 * voltage, charging status) and the battery measurements of its charger block.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/sys/printk.h>
#include "drivers/pmic/npm1300.h"
#include "system/logging.h"
#include "main.h"
#include "drivers/gpio/board_gpio.h"

/* Copy of the private config struct of drivers/regulator/regulator_npm1300.c */
struct regulator_npm1300_config {
	struct regulator_common_config common;
	const struct device *mfd;
	uint8_t source;
	int32_t retention_uv;
	struct gpio_dt_spec enable_gpios;
	struct gpio_dt_spec retention_gpios;
	struct gpio_dt_spec pwm_gpios;
};

#define PMIC_LDO1  DEVICE_DT_GET(DT_NODELABEL(pmic_ldo1))
#define PMIC_LDO2  DEVICE_DT_GET(DT_NODELABEL(pmic_ldo2))
#define PMIC_BUCK2 DEVICE_DT_GET(DT_NODELABEL(pmic_buck2))

#define NPM_MFD(dev) (((const struct regulator_npm1300_config *)(dev)->config)->mfd)

#define CHGR_BASE            0x03
#define CHGR_OFFSET_EN_SET   0x04
#define CHGR_OFFSET_VTERM    0x0C
#define CHGR_OFFSET_CHG_STAT 0x34

/* The IMU and microphone supply (LDSW2) has to be on, and the LSM6DSO through its
 * boot time, before the sensor drivers initialise. */
#define IMU_MIC_SUPPLY_INIT_PRIORITY 87
#define LSM6DSO_BOOT_TIME_MS         10

BUILD_ASSERT(CONFIG_REGULATOR_NPM1300_INIT_PRIORITY < IMU_MIC_SUPPLY_INIT_PRIORITY &&
	     IMU_MIC_SUPPLY_INIT_PRIORITY < CONFIG_SENSOR_INIT_PRIORITY);

/* LDSW1: the 1.8 V rail of the panel and the light sensor */
int npm1300_ldsw1_set(bool on)
{
	int ret = mfd_npm1300_reg_write(NPM_MFD(PMIC_LDO1), 0x08, on ? 0x00 : 0x01, 1);

	if (ret < 0) {
		printk("Could not set ldsw1.\n");
		return ret;
	}
	printk("%s ldsw1 1.8v for panel\n", on ? "enable" : "disable");
	return 0;
}

int npm1300_enter_ship_mode(void)
{
	int ret = mfd_npm1300_reg_write(NPM_MFD(PMIC_BUCK2), 0x0b, 0x02, 1);

	if (ret < 0) {
		printk("Could not set shipmode.\n");
	} else {
		printk("enable for shipmode\n");
	}
	return ret;
}
int power_for_panel(void)
{
	const struct regulator_npm1300_config *config;
	int ret;

	ret = mfd_npm1300_reg_write(NPM_MFD(PMIC_LDO1), 0x08, 0x00, 1);
	if (ret < 0) {
		G1_LOG(0, "Could not set ldsw1.\n");
		return 0;
	}
	G1_LOG(0, "enable ldsw1 1.8v for panel\n");
	k_msleep(1);

	config = PMIC_BUCK2->config;
	if (GLOBAL_STATE->panel.voltage == 1) {
		ret = mfd_npm1300_reg_write(config->mfd, 0x04, 0x0a, 1);
		if (ret < 0) {
			G1_LOG(0, "Could not set buck2 to 1.1v.\n");
			return 0;
		}
		G1_LOG(0, "set buck2 to 1.1v\n");
	} else {
		ret = mfd_npm1300_reg_write(config->mfd, 0x04, 0x0a, 2);
		if (ret < 0) {
			G1_LOG(0, "Could not set buck2 to 1.2v.\n");
			return 0;
		}
		G1_LOG(0, "set buck2 to 1.2v\n");
	}
	ret = mfd_npm1300_reg_update(config->mfd, 0x04, 0x0f, 2, 2);
	if (ret < 0) {
		G1_LOG(0, "Could not enable software control\n");
		return 0;
	}
	ret = mfd_npm1300_reg_write(config->mfd, 0x04, 0x02, 1);
	if (ret < 0) {
		G1_LOG(0, "Could not set buck2.\n");
		return 0;
	}
	G1_LOG(0, "enable buck2.\n");
	k_msleep(1);
	panel_gpio_enable();
	k_msleep(15);
	G1_LOG(0, "turn on -2v for panel.\n");
	return 0;
}

static int power_for_imu_and_mic(void)
{
	int ret;

	if (!device_is_ready(PMIC_LDO2)) {
		G1_LOG(0, "ldsw2 not ready.\n");
		return -ENODEV;
	}
	ret = regulator_enable(PMIC_LDO2);
	if (ret < 0) {
		G1_LOG(0, "Could not set ldsw2.\n");
		return ret;
	}
	G1_LOG(0, "enable ldsw2 for imu and mic\n");
	k_msleep(LSM6DSO_BOOT_TIME_MS);
	return 0;
}

SYS_INIT(power_for_imu_and_mic, POST_KERNEL, IMU_MIC_SUPPLY_INIT_PRIORITY);

int power_down_panel(void)
{
	int ret;

	k_msleep(2);
	panel_gpio_disable();
	G1_LOG(0, "turn off -2v for panel.\n");
	k_msleep(16);
	ret = mfd_npm1300_reg_write(NPM_MFD(PMIC_BUCK2), 0x04, 0x03, 1);
	if (ret < 0) {
		G1_LOG(0, "Could not set buck2.\n");
		return 0;
	}
	G1_LOG(0, "disable buck2 1.2v\n");
	k_msleep(5);
	ret = mfd_npm1300_reg_write(NPM_MFD(PMIC_LDO1), 0x08, 0x01, 1);
	if (ret < 0) {
		G1_LOG(0, "Could not set ldsw1.\n");
		return 0;
	}
	G1_LOG(0, "disable ldsw1 1.8v for panel\n");
	return 0;
}

int power_down_imu_and_mic(void)
{
	int ret;

	ret = mfd_npm1300_reg_write(NPM_MFD(PMIC_LDO2), 0x08, 0x03, 1);
	if (ret < 0) {
		G1_LOG(0, "Could not set ldsw2.\n");
		return 0;
	}
	G1_LOG(0, "disable ldsw2 for imu and mic\n");
	return 0;
}

int dump_pmic_state(void)
{
	const struct regulator_npm1300_config *config = PMIC_LDO2->config;
	uint8_t val = 0;

	mfd_npm1300_reg_write(config->mfd, 0x03, 0x10, 0xeb);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x11, 0x01);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x12, 0xad);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x13, 0x01);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x14, 0x3f);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x15, 0x03);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x16, 0x2e);
	mfd_npm1300_reg_write(config->mfd, 0x03, 0x17, 0x02);

	mfd_npm1300_reg_read(config->mfd, 0x03, 0x10, &val);
	printk("0x10 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x11, &val);
	printk("0x11 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x12, &val);
	printk("0x12 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x13, &val);
	printk("0x13 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x14, &val);
	printk("0x14 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x15, &val);
	printk("0x15 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x16, &val);
	printk("0x16 is %x\n", val);
	mfd_npm1300_reg_read(config->mfd, 0x03, 0x17, &val);
	printk("0x17 is %x\n", val);
	return 0;
}

int change_termination_voltage(void)
{
	/* VTERM = 4.20 V */
	if (mfd_npm1300_reg_write(NPM_MFD(PMIC_LDO2), CHGR_BASE, CHGR_OFFSET_VTERM, 8) < 0) {
		printk("Could not change termination voltage to 4.2V\n");
		return -1;
	}
	return 0;
}

int enable_charger(void)
{
	if (mfd_npm1300_reg_write(NPM_MFD(PMIC_LDO2), CHGR_BASE, CHGR_OFFSET_EN_SET, 1) < 0) {
		printk("Could not enable charger\n");
		return -1;
	}
	return 0;
}

/* returns 1 while charging, 0 when charging completed / trickle */
int set_charger_status(void)
{
	uint8_t status = 0;
	bool trickle, cc, cv;

	if (mfd_npm1300_reg_read(NPM_MFD(PMIC_LDO2), CHGR_BASE, CHGR_OFFSET_CHG_STAT, &status) <
	    0) {
		printk("Could not get charger status\n");
		return -1;
	}
	trickle = status & BIT(2);
	cc = status & BIT(3);
	cv = status & BIT(4);

	if (trickle || cc) {
		return 0;
	}
	return !cv;
}

int npm1300_read_battery(const struct device *charger, float *voltage, float *current, float *temp)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch(charger);
	if (ret < 0) {
		return ret;
	}

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
	*voltage = (float)value.val1 + ((float)value.val2 / 1000000);

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
	*temp = (float)value.val1 + ((float)value.val2 / 1000000);

	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
	*current = (float)value.val1 + ((float)value.val2 / 1000000);

	return 0;
}
