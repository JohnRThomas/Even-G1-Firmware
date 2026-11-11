#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <string.h>
#include <zephyr/kernel.h>

#include "harness.h"
#include "fakes_power.h"

DEFINE_FAKE_VALUE_FUNC(int, nrf_fuel_gauge_init, const struct nrf_fuel_gauge_init_parameters *,
		       float *);
DEFINE_FAKE_VALUE_FUNC(float, nrf_fuel_gauge_process, float, float, float, float,
		       struct nrf_fuel_gauge_state_info *);
DEFINE_FAKE_VALUE_FUNC(uint8_t, pt_comm_busy_get);
DEFINE_FAKE_VALUE_FUNC(int, lock_sensor_mutex);
DEFINE_FAKE_VALUE_FUNC(int, unlock_sensor_mutex);
DEFINE_FAKE_VALUE_FUNC(char *, get_glasses_name);
DEFINE_FAKE_VOID_FUNC(report_event_to_phone, int);
DEFINE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VOID_FUNC(exit_running_task, struct GlassesState *, int);

struct nrf_fuel_gauge_init_parameters g1_fg_init_params;
char g1_power_glasses_name[32];
__weak uint8_t fuel_gauge_stuck;

static int fg_init_record(const struct nrf_fuel_gauge_init_parameters *p, float *v0)
{
	ARG_UNUSED(v0);
	g1_fg_init_params = *p;
	return nrf_fuel_gauge_init_fake.return_val;
}

static char *glasses_name_default(void)
{
	return g1_power_glasses_name;
}

static void fakes_power_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(nrf_fuel_gauge_init);
	RESET_FAKE(nrf_fuel_gauge_process);
	RESET_FAKE(pt_comm_busy_get);
	RESET_FAKE(lock_sensor_mutex);
	RESET_FAKE(unlock_sensor_mutex);
	RESET_FAKE(get_glasses_name);
	RESET_FAKE(report_event_to_phone);
	RESET_FAKE(sync_to_slave);
	RESET_FAKE(sync_to_master);
	RESET_FAKE(exit_running_task);
	nrf_fuel_gauge_init_fake.custom_fake = fg_init_record;
	get_glasses_name_fake.custom_fake = glasses_name_default;
	memset(&g1_fg_init_params, 0, sizeof(g1_fg_init_params));
	strcpy(g1_power_glasses_name, "S110LAAL290026");
	fuel_gauge_stuck = 0;
}

G1_UNIT_RESET_HOOK(fakes_power_reset);
