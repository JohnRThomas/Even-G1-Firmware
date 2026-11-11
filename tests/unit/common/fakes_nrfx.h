/*
 * nrfx PDM / SPIM fakes and host glue for Zephyr's nRF drivers. Peripheral events fire
 * synchronously.
 * PDM: the first nrfx_pdm_buffer_set() starts the peripheral and at once requests the second
 * buffer; nrfx_pdm_stop() releases both. g1_fake_pdm_capture() fills the active buffer, releases it
 * and requests the next. A suite running the Zephyr driver must stop it before the next test.
 * SPIM: nrfx_spim_xfer() completes before returning (handler included) and returns
 * NRFX_ERROR_INVALID_STATE when uninitialised, where nrfx asserts. xfer_error applies after
 * xfer_error_skip successful transfers, for xfer_error_count of them (0 = all).
 */
#ifndef G1_UNIT_FAKES_NRFX_H_
#define G1_UNIT_FAKES_NRFX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>

#include <nrfx_errors.h>
#include <nrfx_pdm.h>
#include <nrfx_spim.h>
#include <hal/nrf_pdm.h>

DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_init, nrfx_pdm_config_t const *,
			nrfx_pdm_event_handler_t);
DECLARE_FAKE_VOID_FUNC(nrfx_pdm_uninit);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_start);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_stop);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_buffer_set, int16_t *, uint16_t);
DECLARE_FAKE_VOID_FUNC(nrfx_pdm_irq_handler);
DECLARE_FAKE_VOID_FUNC(nrf_pdm_gain_set, NRF_PDM_Type *, nrf_pdm_gain_t, nrf_pdm_gain_t);

enum g1_fake_pdm_op {
	G1_PDM_OP_INIT = 1,
	G1_PDM_OP_UNINIT,
	G1_PDM_OP_START,
	G1_PDM_OP_STOP,
	G1_PDM_OP_BUFFER_SET,
	G1_PDM_OP_GAIN_SET,
	G1_PDM_OP_EVT_REQUEST,
	G1_PDM_OP_EVT_RELEASE,
	G1_PDM_OP_EVT_OVERFLOW,
};

enum g1_fake_pdm_state {
	G1_PDM_IDLE = 0,
	G1_PDM_STARTING,
	G1_PDM_RUNNING,
	G1_PDM_STOPPING,
};

#define G1_FAKE_PDM_LOG_MAX 64

struct g1_fake_pdm {
	bool initialized;
	bool powered;                   /* ENABLE + START issued */
	enum g1_fake_pdm_state op_state;
	nrfx_pdm_config_t config;
	nrfx_pdm_event_handler_t handler;
	int16_t *buffer[2];
	uint16_t length[2];             /* samples */
	uint8_t active;                 /* buffer the DMA is filling */
	uint32_t gain_l_at_start;
	uint32_t gain_r_at_start;
	int captured_blocks;
	int log_count;
	struct {
		enum g1_fake_pdm_op op;
		uintptr_t a;            /* INIT: clock_freq, GAIN_SET: gain_l, else buffer */
		uintptr_t b;            /* BUFFER_SET: length, GAIN_SET: gain_r */
	} log[G1_FAKE_PDM_LOG_MAX];
};

extern struct g1_fake_pdm g1_fake_pdm;

int g1_fake_pdm_capture(const int16_t *samples, size_t n);
int g1_fake_pdm_overflow(void);
int g1_fake_pdm_log_find(enum g1_fake_pdm_op op, int from);

DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_init, nrfx_spim_t const *,
			nrfx_spim_config_t const *, nrfx_spim_evt_handler_t, void *);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_reconfigure, nrfx_spim_t const *,
			nrfx_spim_config_t const *);
DECLARE_FAKE_VOID_FUNC(nrfx_spim_uninit, nrfx_spim_t const *);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_xfer, nrfx_spim_t const *,
			nrfx_spim_xfer_desc_t const *, uint32_t);
DECLARE_FAKE_VOID_FUNC(nrfx_spim_abort, nrfx_spim_t const *);
DECLARE_FAKE_VOID_FUNC(nrfx_spim_3_irq_handler);
DECLARE_FAKE_VOID_FUNC(nrfx_spim_4_irq_handler);

enum g1_fake_spim_op {
	G1_SPIM_OP_INIT = 1,
	G1_SPIM_OP_RECONFIGURE,
	G1_SPIM_OP_UNINIT,
	G1_SPIM_OP_XFER,
	G1_SPIM_OP_ABORT,
};

#define G1_FAKE_SPIM_LOG_MAX   512
#define G1_FAKE_SPIM_POOL_SIZE (1024 * 1024)
#define G1_FAKE_SPIM_HEAD      8
#define G1_FAKE_SPIM_MAXCNT    0xffff
#define G1_FAKE_SPIM_RESP_MAX  8
#define G1_FAKE_SPIM_RESP_LEN  16

struct g1_spim_entry {
	enum g1_fake_spim_op op;
	uint8_t inst;          /* 0xff: unknown register block */
	nrfx_err_t result;
	uint32_t t_us;         /* simulated time, us */
	/* G1_SPIM_OP_XFER only */
	uint32_t flags;
	size_t tx_len;
	size_t rx_len;
	size_t len;            /* max(tx_len, rx_len); 0 on failure */
	const uint8_t *mosi;   /* NULL once the pool is full */
	uint8_t head[G1_FAKE_SPIM_HEAD];
};

struct g1_fake_spim {
	struct {
		bool initialized;
		nrfx_spim_config_t config;
		nrfx_spim_evt_handler_t handler;
		void *context;
	} inst[5];
	/* set by the test */
	nrfx_err_t xfer_error;
	int xfer_error_skip;
	int xfer_error_count;
	uint8_t miso_fill;
	int resp_count;
	struct {
		uint8_t opcode;
		uint8_t len;
		uint8_t miso[G1_FAKE_SPIM_RESP_LEN];
	} resp[G1_FAKE_SPIM_RESP_MAX];
	int count;
	int dropped;
	int xfers;
	int too_long;  /* longer than G1_FAKE_SPIM_MAXCNT (nrfx asserts) */
	struct g1_spim_entry log[G1_FAKE_SPIM_LOG_MAX];
	size_t pool_used;
	uint8_t pool[G1_FAKE_SPIM_POOL_SIZE];
};

extern struct g1_fake_spim g1_fake_spim;

void g1_fake_spim_respond(uint8_t opcode, const uint8_t *miso, size_t n);
int g1_fake_spim_find(enum g1_fake_spim_op op, int from);
int g1_fake_spim_find_cmd(uint8_t opcode, int from);
int g1_fake_spim_xfer_index(int n);
void g1_fake_spim_clear_log(void);

#endif /* G1_UNIT_FAKES_NRFX_H_ */
