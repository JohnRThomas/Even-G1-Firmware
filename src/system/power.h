/*
 * Power policy (power.c): ship mode, the shared panel / light-sensor rail, and
 * the hooks called around external flash accesses.
 */
#ifndef G1_SYSTEM_POWER_H_
#define G1_SYSTEM_POWER_H_

int set_shipmode(int enable);
/* 1.8 V rail shared by the panel and the light sensor, with the sensor init/deinit */
int enable_power_for_panel(void);
int disable_power_for_panel(void);
/* empty hooks called around external flash accesses */
void ext_flash_power_up(void);
void ext_flash_power_down(void);

#endif /* G1_SYSTEM_POWER_H_ */
