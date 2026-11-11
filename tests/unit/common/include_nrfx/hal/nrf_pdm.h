/* Host stand-in for nrfx <hal/nrf_pdm.h>; NRF_PDM0 is a RAM block tests read back. */
#ifndef G1_UNIT_HAL_NRF_PDM_H_
#define G1_UNIT_HAL_NRF_PDM_H_

#include <stdbool.h>
#include <stdint.h>

#define NRF_PDM_HAS_RATIO_CONFIG 1
#define NRF_PDM_HAS_MCLKCONFIG   1

#define NRF_PDM_GAIN_MINIMUM 0x00
#define NRF_PDM_GAIN_DEFAULT 0x28
#define NRF_PDM_GAIN_MAXIMUM 0x50

typedef uint8_t nrf_pdm_gain_t;

/* not the hardware layout */
typedef struct {
	volatile uint32_t ENABLE;
	volatile uint32_t PDMCLKCTRL;
	volatile uint32_t MODE;
	volatile uint32_t GAINL;
	volatile uint32_t GAINR;
	volatile uint32_t RATIO;
	volatile uint32_t MCLKCONFIG;
} NRF_PDM_Type;

extern NRF_PDM_Type g1_fake_nrf_pdm0;
#define NRF_PDM0 (&g1_fake_nrf_pdm0)

#define PDM_PDMCLKCTRL_FREQ_1231K 0x09800000UL
#define PDM_PDMCLKCTRL_FREQ_1280K 0x0A000000UL
#define PDM_PDMCLKCTRL_FREQ_1333K 0x0A800000UL

typedef enum {
	NRF_PDM_FREQ_1000K = 0x08000000UL,
	NRF_PDM_FREQ_1032K = 0x08400000UL,
	NRF_PDM_FREQ_1067K = 0x08800000UL,
	NRF_PDM_FREQ_1231K = PDM_PDMCLKCTRL_FREQ_1231K,
	NRF_PDM_FREQ_1280K = PDM_PDMCLKCTRL_FREQ_1280K,
	NRF_PDM_FREQ_1333K = PDM_PDMCLKCTRL_FREQ_1333K,
} nrf_pdm_freq_t;

typedef enum {
	NRF_PDM_RATIO_64X = 0,
	NRF_PDM_RATIO_80X = 1,
} nrf_pdm_ratio_t;

typedef enum {
	NRF_PDM_MODE_STEREO = 0,
	NRF_PDM_MODE_MONO = 1,
} nrf_pdm_mode_t;

typedef enum {
	NRF_PDM_EDGE_LEFTFALLING = 0,
	NRF_PDM_EDGE_LEFTRISING = 1,
} nrf_pdm_edge_t;

typedef enum {
	NRF_PDM_MCLKSRC_PCLK32M = 0,
	NRF_PDM_MCLKSRC_ACLK = 1,
} nrf_pdm_mclksrc_t;

#define G1_PDM_MODE_OPERATION_POS 0
#define G1_PDM_MODE_EDGE_POS      1

/* not static inline, so it can be faked */
void nrf_pdm_gain_set(NRF_PDM_Type *p_reg, nrf_pdm_gain_t gain_l, nrf_pdm_gain_t gain_r);

#endif /* G1_UNIT_HAL_NRF_PDM_H_ */
