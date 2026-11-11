/* nrf_fuel_gauge_init copies its parameters to g1_fg_init_params. */
#ifndef G1_UNIT_FAKES_POWER_H_
#define G1_UNIT_FAKES_POWER_H_

#include <stdint.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

#include "nrf_fuel_gauge.h"

DECLARE_FAKE_VALUE_FUNC(int, nrf_fuel_gauge_init, const struct nrf_fuel_gauge_init_parameters *,
			float *);
DECLARE_FAKE_VALUE_FUNC(float, nrf_fuel_gauge_process, float, float, float, float,
			struct nrf_fuel_gauge_state_info *);
DECLARE_FAKE_VALUE_FUNC(uint8_t, pt_comm_busy_get);
DECLARE_FAKE_VALUE_FUNC(int, lock_sensor_mutex);
DECLARE_FAKE_VALUE_FUNC(int, unlock_sensor_mutex);
DECLARE_FAKE_VALUE_FUNC(char *, get_glasses_name);
DECLARE_FAKE_VOID_FUNC(report_event_to_phone, int);
DECLARE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);
DECLARE_FAKE_VOID_FUNC(exit_running_task, struct GlassesState *, int);

extern struct nrf_fuel_gauge_init_parameters g1_fg_init_params;
extern char g1_power_glasses_name[32];
extern uint8_t fuel_gauge_stuck;

#endif /* G1_UNIT_FAKES_POWER_H_ */
