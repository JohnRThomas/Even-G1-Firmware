/* Host stand-in for the one nRF register threads.c uses. */
#ifndef G1_UNIT_NFC_NRFX_STUB_H_
#define G1_UNIT_NFC_NRFX_STUB_H_

#include <stdint.h>

struct g1_nrf_reset_network {
	volatile uint32_t FORCEOFF;
};

struct g1_nrf_reset {
	struct g1_nrf_reset_network NETWORK;
};

extern struct g1_nrf_reset g1_nrf_reset_s;

#define NRF_RESET_S                          (&g1_nrf_reset_s)
#define RESET_NETWORK_FORCEOFF_FORCEOFF_Hold 1UL

#endif /* G1_UNIT_NFC_NRFX_STUB_H_ */
