/*
 * System suspend / resume (system_state.c): screen and IMU power state changes,
 * the global suspend and resume sequences, and the watchdog reboot.
 */
#ifndef G1_SYSTEM_SYSTEM_STATE_H_
#define G1_SYSTEM_SYSTEM_STATE_H_

#include <stdint.h>
#include <stdbool.h>
#include "system/glasses_state.h"

int trigger_screen_state_change(const char *who, struct GlassesState *state, uint8_t on);
int trigger_imu_state_change(void *dev, struct GlassesState *state, bool on);
int global_system_suspend(struct GlassesState *state);
int global_system_resume(struct GlassesState *state);
/* reboot through the watchdog: arm a short timeout and let it expire */
void system_reboot_delayed(void);

#endif /* G1_SYSTEM_SYSTEM_STATE_H_ */
