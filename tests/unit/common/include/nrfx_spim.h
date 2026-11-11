/*
 * Host stand-in for nrfx 3.x <nrfx_spim.h>. The IRQ numbers are free native_sim lines (N_IRQS is
 * 32).
 */
#ifndef G1_UNIT_NRFX_SPIM_H_
#define G1_UNIT_NRFX_SPIM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nrfx_errors.h>

typedef struct {
	uint32_t volatile reserved[0x400];
} NRF_SPIM_Type;

typedef struct {
	NRF_SPIM_Type *p_reg;
	uint8_t drv_inst_idx;
} nrfx_spim_t;

_Static_assert(sizeof(nrfx_spim_t) == 8, "spi_master_ctx layout needs an 8-byte nrfx_spim_t");

extern NRF_SPIM_Type g1_fake_nrf_spim3;
extern NRF_SPIM_Type g1_fake_nrf_spim4;
#define NRF_SPIM3 (&g1_fake_nrf_spim3)
#define NRF_SPIM4 (&g1_fake_nrf_spim4)

#define NRFX_SPIM3_INST_IDX 0
#define NRFX_SPIM4_INST_IDX 1

#define NRFX_SPIM_INSTANCE(id)                                                                     \
	{                                                                                          \
		.p_reg = NRF_SPIM##id, .drv_inst_idx = NRFX_SPIM##id##_INST_IDX,                   \
	}

#define G1_FAKE_SPIM3_IRQ 21
#define G1_FAKE_SPIM4_IRQ 22
#ifndef NRFX_IRQ_NUMBER_GET
#define NRFX_IRQ_NUMBER_GET(base_addr)                                                             \
	((const void *)(base_addr) == (const void *)NRF_SPIM3 ? G1_FAKE_SPIM3_IRQ : G1_FAKE_SPIM4_IRQ)
#endif

#define NRF_SPIM_PIN_NOT_CONNECTED 0xFFFFFFFF

typedef enum {
	NRF_SPIM_MODE_0,
	NRF_SPIM_MODE_1,
	NRF_SPIM_MODE_2,
	NRF_SPIM_MODE_3,
} nrf_spim_mode_t;

typedef enum {
	NRF_SPIM_BIT_ORDER_MSB_FIRST = 0,
	NRF_SPIM_BIT_ORDER_LSB_FIRST = 1,
} nrf_spim_bit_order_t;

typedef struct {
	uint32_t sck_pin;
	uint32_t mosi_pin;
	uint32_t miso_pin;
	uint32_t ss_pin;
	bool ss_active_high;
	uint8_t irq_priority;
	uint8_t orc; /* clocked out after the TX buffer */
	uint32_t frequency; /* Hz */
	nrf_spim_mode_t mode;
	nrf_spim_bit_order_t bit_order;
	uint32_t miso_pull;
	/* SPIM4 extended configuration */
	uint32_t dcx_pin;
	uint8_t rx_delay;    /* 64 MHz cycles */
	bool use_hw_ss;
	uint8_t ss_duration; /* 64 MHz cycles */
	bool skip_gpio_cfg;
	bool skip_psel_cfg;
} nrfx_spim_config_t;

typedef struct {
	uint8_t const *p_tx_buffer;
	size_t tx_length;
	uint8_t *p_rx_buffer;
	size_t rx_length;
} nrfx_spim_xfer_desc_t;

#define NRFX_SPIM_SINGLE_XFER(p_tx, tx_len, p_rx, rx_len)                                          \
	{                                                                                          \
		.p_tx_buffer = (uint8_t const *)(p_tx), .tx_length = (tx_len),                     \
		.p_rx_buffer = (p_rx), .rx_length = (rx_len),                                      \
	}
#define NRFX_SPIM_XFER_TRX(p_tx_buf, tx_length, p_rx_buf, rx_length)                              \
	NRFX_SPIM_SINGLE_XFER(p_tx_buf, tx_length, p_rx_buf, rx_length)
#define NRFX_SPIM_XFER_TX(p_buf, length) NRFX_SPIM_SINGLE_XFER(p_buf, length, NULL, 0)
#define NRFX_SPIM_XFER_RX(p_buf, length) NRFX_SPIM_SINGLE_XFER(NULL, 0, p_buf, length)

#define NRFX_SPIM_FLAG_TX_POSTINC          (1UL << 0)
#define NRFX_SPIM_FLAG_RX_POSTINC          (1UL << 1)
#define NRFX_SPIM_FLAG_NO_XFER_EVT_HANDLER (1UL << 2)
#define NRFX_SPIM_FLAG_HOLD_XFER           (1UL << 3)
#define NRFX_SPIM_FLAG_REPEATED_XFER       (1UL << 4)

typedef enum {
	NRFX_SPIM_EVENT_DONE,
} nrfx_spim_evt_type_t;

typedef struct {
	nrfx_spim_evt_type_t type;
	nrfx_spim_xfer_desc_t xfer_desc;
} nrfx_spim_evt_t;

typedef void (*nrfx_spim_evt_handler_t)(nrfx_spim_evt_t const *p_event, void *p_context);

nrfx_err_t nrfx_spim_init(nrfx_spim_t const *p_instance, nrfx_spim_config_t const *p_config,
			  nrfx_spim_evt_handler_t handler, void *p_context);
nrfx_err_t nrfx_spim_reconfigure(nrfx_spim_t const *p_instance,
				 nrfx_spim_config_t const *p_config);
void nrfx_spim_uninit(nrfx_spim_t const *p_instance);
nrfx_err_t nrfx_spim_xfer(nrfx_spim_t const *p_instance, nrfx_spim_xfer_desc_t const *p_xfer_desc,
			  uint32_t flags);
void nrfx_spim_abort(nrfx_spim_t const *p_instance);
void nrfx_spim_3_irq_handler(void);
void nrfx_spim_4_irq_handler(void);

#endif /* G1_UNIT_NRFX_SPIM_H_ */
