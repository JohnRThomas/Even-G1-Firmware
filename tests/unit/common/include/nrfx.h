/*
 * Host stand-in for <nrfx.h>: the error codes (NRFX_SUCCESS is not 0) and the one register
 * threads.c uses.
 */
#ifndef G1_UNIT_NRFX_H_
#define G1_UNIT_NRFX_H_

#include <stdint.h>

#define NRFX_ERROR_BASE_NUM 0x0BAD0000

typedef enum {
  NRFX_SUCCESS = (NRFX_ERROR_BASE_NUM + 0),
  NRFX_ERROR_INTERNAL = (NRFX_ERROR_BASE_NUM + 1),
  NRFX_ERROR_INVALID_PARAM = (NRFX_ERROR_BASE_NUM + 4),
  NRFX_ERROR_INVALID_STATE = (NRFX_ERROR_BASE_NUM + 5),
  NRFX_ERROR_BUSY = (NRFX_ERROR_BASE_NUM + 11),
} nrfx_err_t;

struct g1_nrf_reset_network {
  volatile uint32_t FORCEOFF;
};

struct g1_nrf_reset {
  struct g1_nrf_reset_network NETWORK;
};

/* defined by the suites that reach its use */
extern struct g1_nrf_reset g1_nrf_reset_s;

#define NRF_RESET_S                          (&g1_nrf_reset_s)
#define RESET_NETWORK_FORCEOFF_FORCEOFF_Hold 1UL

#endif /* G1_UNIT_NRFX_H_ */
