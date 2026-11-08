/*
 * Low-power mode in the charging case (low_power.c).
 */
#ifndef G1_SYSTEM_LOW_POWER_H_
#define G1_SYSTEM_LOW_POWER_H_

#include <stdint.h>

extern uint8_t can_enter_low_power;

void try_enter_low_power_mode(void);
void update_activity_time(void);

#endif /* G1_SYSTEM_LOW_POWER_H_ */
