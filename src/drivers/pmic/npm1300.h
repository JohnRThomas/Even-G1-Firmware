/*
 * nPM1300 PMIC register access (npm1300.c): power rails, ship mode, charger and
 * battery measurements.
 */
#ifndef G1_DRIVERS_PMIC_NPM1300_H_
#define G1_DRIVERS_PMIC_NPM1300_H_

#include <stdbool.h>
#include <zephyr/device.h>

/* rails */
int power_for_panel(void);        /* LDSW1 1.8 V, BUCK2 1.1/1.2 V and the -2 V line */
int power_down_panel(void);
int power_down_imu_and_mic(void); /* LDSW2 (switched on at boot by a SYS_INIT hook) */
int npm1300_ldsw1_set(bool on);   /* the 1.8 V rail alone */
int npm1300_enter_ship_mode(void);
int dump_pmic_state(void);

/* charger */
int change_termination_voltage(void); /* VTERM = 4.20 V */
int enable_charger(void);
int set_charger_status(void); /* reads the charging state: 0 while trickle / CC charging */

/* battery voltage (V), average current (A) and temperature (C) from the charger block */
int npm1300_read_battery(const struct device *charger, float *voltage, float *current, float *temp);

#endif /* G1_DRIVERS_PMIC_NPM1300_H_ */
