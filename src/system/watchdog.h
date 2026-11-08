/*
 * Hardware watchdog (watchdog.c).
 */
#ifndef G1_SYSTEM_WATCHDOG_H_
#define G1_SYSTEM_WATCHDOG_H_

int init_watchdog(void);
int config_watchdog_wait_reset(void);
int watchdog_feed(void);
int watchdog_disable(void);

#endif /* G1_SYSTEM_WATCHDOG_H_ */
