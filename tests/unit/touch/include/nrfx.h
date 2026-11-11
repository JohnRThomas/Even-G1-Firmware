/* Host stand-in for the one nRF register threads.c uses. */
#ifndef G1_UNIT_TOUCH_NRFX_H_
#define G1_UNIT_TOUCH_NRFX_H_

#include <stdint.h>

struct g1_unit_nrf_reset_network {
	volatile uint32_t FORCEOFF;
};

struct g1_unit_nrf_reset {
	struct g1_unit_nrf_reset_network NETWORK;
};

extern struct g1_unit_nrf_reset g1_unit_nrf_reset_s;

#define NRF_RESET_S (&g1_unit_nrf_reset_s)
#define RESET_NETWORK_FORCEOFF_FORCEOFF_Hold 1UL

#endif /* G1_UNIT_TOUCH_NRFX_H_ */
