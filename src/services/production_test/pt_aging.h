/*
 * Declarations of pt_aging.c.
 */
#ifndef G1_SERVICES_PT_PT_AGING_H_
#define G1_SERVICES_PT_PT_AGING_H_

#include "services/production_test/production_test.h"

void enable_charger_retry(void);
uint8_t get_aging_charge_limit(void);
uint8_t get_aging_phase(void);                /* 1 charging, 2 discharging */
void set_aging_mode(uint8_t v);
uint8_t get_aging_mode(void);
void enter_aging_mode(void);
void exit_aging_mode(void);
void aging_charge_test(int interval);
void aging_charge_check(void);
void start_aging_mode_thread(void);

#endif /* G1_SERVICES_PT_PT_AGING_H_ */
