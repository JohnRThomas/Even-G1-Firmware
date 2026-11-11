/* Stand-in for the Arm-only nRF Fuel Gauge library header. */
#ifndef G1_UNIT_NRF_FUEL_GAUGE_STUB_H_
#define G1_UNIT_NRF_FUEL_GAUGE_STUB_H_

#include <stddef.h>

struct battery_model {
	float param_1[201];
	float temps[3];
	float param_2[122];
	float param_3[201];
	float param_4[201];
	float param_5[201];
	float param_6[201];
	float param_7[122];
	float param_8[122];
	float param_9[3];
	float param_10[6];
	float param_11[6];
	float param_12[3];
	char name[64];
};

struct nrf_fuel_gauge_state_info {
	float yhat;
	float r0;
	float T_truncated;
};

struct nrf_fuel_gauge_init_parameters {
	float v0; /* V */
	float i0; /* A */
	float t0; /* C */
	const struct battery_model *model;
};

int nrf_fuel_gauge_init(const struct nrf_fuel_gauge_init_parameters *parameters, float *v0);

/* returns SoC in % */
float nrf_fuel_gauge_process(float v, float i, float T, float t_delta,
			     struct nrf_fuel_gauge_state_info *state);

#endif /* G1_UNIT_NRF_FUEL_GAUGE_STUB_H_ */
