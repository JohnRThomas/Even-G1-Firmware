/*
 * Wall-clock time of the glasses: the UTC second counter set by the phone and
 * carried in every lens-to-lens sync packet, a millisecond wall clock, and the
 * boot-relative second counter.
 */
#include <zephyr/kernel.h>
#include "system/clock.h"
#include "system/glasses_state.h"
#include "system/logging.h"

static uint32_t boot_uptime_s;
static uint32_t duration_ref_ms[4];
static int64_t time_base_uptime;
static int64_t time_base;
/* accumulates whole seconds elapsed since the last call into *total_s */
void accumulate_elapsed_seconds(uint32_t *total_s, int idx, int restart)
{
	uint32_t now, d;

	if (restart) {
		duration_ref_ms[idx] = k_uptime_get();
	} else {
		now = k_uptime_get();
		if (duration_ref_ms[idx] == 0) {
			duration_ref_ms[idx] = now;
		} else {
			d = now - duration_ref_ms[idx];
			if (d >= 1000) {
				d /= 1000;
				*total_s += d;
				duration_ref_ms[idx] += d * 1000;
			}
		}
	}
}

/* sets the wall clock */
void set_wall_clock_ms(int64_t ts)
{
	time_base = ts;
	time_base_uptime = k_uptime_get();
}

/* current wall clock */
void get_wall_clock_ms(int64_t *ts)
{
	*ts = k_uptime_get() - time_base_uptime + time_base;
}

void init_boot_seconds(void)
{
	boot_uptime_s = k_uptime_get() / 1000;
}

int get_boot_seconds(void)
{
	int s = k_uptime_get() / 1000 - boot_uptime_s;

	G1_LOG_INF("[csh_debug] bootSeconds is %d\n", s);

	return s;
}

uint32_t get_date(void)
{
	return get_dashboard_state()->date_ts;
}

uint32_t set_date(uint32_t ts)
{
	get_dashboard_state()->date_ts = ts;
	return ts;
}
