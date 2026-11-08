/*
 * Periodic system monitor (monitor.c): wear and charger state, watchdog feeding,
 * event reports to the phone.
 */
#ifndef G1_SYSTEM_MONITOR_H_
#define G1_SYSTEM_MONITOR_H_

#include <stdint.h>
#include <zephyr/device.h>
#include "system/glasses_state.h"

extern uint8_t fuel_gauge_stuck;  /* battery level stuck: stop feeding the watchdog */
extern uint8_t watchdog_enabled;

uint8_t get_wearing_state(void);
uint8_t get_wear_state_event(void);
uint8_t get_glasses_ble_status(void);
const struct device *get_charger_device(void);
void monitor_start(struct GlassesState *state);

#endif /* G1_SYSTEM_MONITOR_H_ */
