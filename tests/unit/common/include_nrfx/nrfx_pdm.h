/* Host stand-in for nrfx 3.x <nrfx_pdm.h>. */
#ifndef G1_UNIT_NRFX_PDM_H_
#define G1_UNIT_NRFX_PDM_H_

#include <stdbool.h>
#include <stdint.h>
#include <nrfx_errors.h>
#include <hal/nrf_pdm.h>

#define NRFX_PDM_MAX_BUFFER_SIZE 32767

#define NRFX_PDM_DEFAULT_CONFIG_IRQ_PRIORITY 7

typedef enum {
	NRFX_PDM_NO_ERROR = 0,
	NRFX_PDM_ERROR_OVERFLOW = 1,
} nrfx_pdm_error_t;

typedef struct {
	bool buffer_requested;
	int16_t *buffer_released;
	nrfx_pdm_error_t error;
} nrfx_pdm_evt_t;

typedef struct {
	nrf_pdm_mode_t mode;
	nrf_pdm_edge_t edge;
	uint32_t clk_pin;
	uint32_t din_pin;
	nrf_pdm_freq_t clock_freq;
	nrf_pdm_gain_t gain_l;
	nrf_pdm_gain_t gain_r;
	uint8_t interrupt_priority;
	nrf_pdm_ratio_t ratio;
	nrf_pdm_mclksrc_t mclksrc;
	bool skip_gpio_cfg;
	bool skip_psel_cfg;
} nrfx_pdm_config_t;

#define NRFX_PDM_DEFAULT_CONFIG(_pin_clk, _pin_din)                                                \
	{                                                                                          \
		.mode = NRF_PDM_MODE_MONO,                                                         \
		.edge = NRF_PDM_EDGE_LEFTFALLING,                                                  \
		.clk_pin = _pin_clk,                                                               \
		.din_pin = _pin_din,                                                               \
		.clock_freq = NRF_PDM_FREQ_1032K,                                                  \
		.gain_l = NRF_PDM_GAIN_DEFAULT,                                                    \
		.gain_r = NRF_PDM_GAIN_DEFAULT,                                                    \
		.interrupt_priority = NRFX_PDM_DEFAULT_CONFIG_IRQ_PRIORITY,                        \
		.ratio = NRF_PDM_RATIO_64X,                                                        \
		.mclksrc = NRF_PDM_MCLKSRC_PCLK32M,                                                \
	}

typedef void (*nrfx_pdm_event_handler_t)(nrfx_pdm_evt_t const *p_evt);

nrfx_err_t nrfx_pdm_init(nrfx_pdm_config_t const *p_config, nrfx_pdm_event_handler_t event_handler);
void nrfx_pdm_uninit(void);
nrfx_err_t nrfx_pdm_start(void);
nrfx_err_t nrfx_pdm_stop(void);
nrfx_err_t nrfx_pdm_buffer_set(int16_t *buffer, uint16_t buffer_length);
void nrfx_pdm_irq_handler(void);

/* nrfx_glue.h ISR trampoline */
void nrfx_isr(const void *irq_handler);

#endif /* G1_UNIT_NRFX_PDM_H_ */
