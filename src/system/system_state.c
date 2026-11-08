/*
 * Screen on/off state, IMU on/off hooks and global suspend/resume.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include "system/logging.h"
#include "system/system_state.h"
#include "system/monitor.h"
#include "system/watchdog.h"

int trigger_screen_state_change(const char *who, struct GlassesState *state, uint8_t on)
{
	if (state->screen_on == on) {
		G1_LOG_WRN("%s: ignore:%d.\n", who, state->screen_on);
		return -1;
	}
	if (on) {
		state->screen_on = 1;
		k_sem_give(&state->screen_on_sem);
	} else {
		state->screen_on = 0;
	}
	G1_LOG_WRN("%s: trigger_on_screen -> %d\n", who, state->screen_on);
	return on;
}

static int set_imu_on_state(struct GlassesState *state, uint32_t on)
{
	if (state->imu.on != on) {
		state->imu.on = on;
		if (state->display_mode == 10 && on && !state->imu_busy) {
			state->imu_event = 1;
			k_sem_give(&state->imu_sem);
		}
		return 0;
	}
	return -1;
}

int trigger_imu_state_change(void *dev, struct GlassesState *state, bool on)
{
	set_imu_on_state(state, on);
	trigger_screen_state_change(on ? "imu-on" : "imu-off", state, on);
	return 0;
}

int global_system_suspend(struct GlassesState *state)
{
	if (state->suspend_state == 0 || state->suspend_state == 4) {
		state->suspend_state = 2;
		G1_LOG_WRN("begin.\n");
		k_sleep(K_MSEC(300));
		state->panel.suspend_cb(&state->panel);
		G1_LOG_WRN("end.\n");
		state->suspend_state = 1;
	}
	return 0;
}

int global_system_resume(struct GlassesState *state)
{
	if (state->suspend_state == 1 || state->suspend_state == 5) {
		state->suspend_state = 3;
		G1_LOG_WRN("begin.\n");
		state->panel.resume_cb(&state->panel);
		G1_LOG_WRN("end.\n");
		state->suspend_state = 0;
	}
	return 0;
}

/* reboot through the watchdog: arm a short timeout and let it expire */
void system_reboot_delayed(void)
{
	fuel_gauge_stuck = 1;
	config_watchdog_wait_reset();
}
