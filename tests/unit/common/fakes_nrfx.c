#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include "fakes_nrfx.h"
#include "harness.h"

/* ---- PDM ---- */

DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_init, nrfx_pdm_config_t const *,
		       nrfx_pdm_event_handler_t);
DEFINE_FAKE_VOID_FUNC(nrfx_pdm_uninit);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_start);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_stop);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_buffer_set, int16_t *, uint16_t);
DEFINE_FAKE_VOID_FUNC(nrfx_pdm_irq_handler);
DEFINE_FAKE_VOID_FUNC(nrf_pdm_gain_set, NRF_PDM_Type *, nrf_pdm_gain_t, nrf_pdm_gain_t);

__weak NRF_PDM_Type g1_fake_nrf_pdm0;
struct g1_fake_pdm g1_fake_pdm;

static void pdm_log(enum g1_fake_pdm_op op, uintptr_t a, uintptr_t b)
{
	if (g1_fake_pdm.log_count < G1_FAKE_PDM_LOG_MAX) {
		g1_fake_pdm.log[g1_fake_pdm.log_count].op = op;
		g1_fake_pdm.log[g1_fake_pdm.log_count].a = a;
		g1_fake_pdm.log[g1_fake_pdm.log_count].b = b;
		g1_fake_pdm.log_count++;
	}
}

int g1_fake_pdm_log_find(enum g1_fake_pdm_op op, int from)
{
	for (int i = MAX(from, 0); i < g1_fake_pdm.log_count; i++) {
		if (g1_fake_pdm.log[i].op == op) {
			return i;
		}
	}
	return -1;
}

static void pdm_event(bool requested, int16_t *released, nrfx_pdm_error_t error)
{
	const nrfx_pdm_evt_t evt = {
		.buffer_requested = requested,
		.buffer_released = released,
		.error = error,
	};

	if (error != NRFX_PDM_NO_ERROR) {
		pdm_log(G1_PDM_OP_EVT_OVERFLOW, 0, 0);
	} else if (released) {
		pdm_log(G1_PDM_OP_EVT_RELEASE, (uintptr_t)released, requested);
	} else {
		pdm_log(G1_PDM_OP_EVT_REQUEST, 0, 0);
	}
	if (g1_fake_pdm.handler) {
		g1_fake_pdm.handler(&evt);
	}
}

/* STARTED: the DMA has latched the next buffer */
static void pdm_irq_started(void)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;
	uint8_t finished = p->active;
	uint8_t next = (~p->active) & 0x01;

	if (p->buffer[next] || p->op_state == G1_PDM_STARTING) {
		int16_t *released = NULL;

		if (p->op_state == G1_PDM_STARTING) {
			p->op_state = G1_PDM_RUNNING;
		} else {
			released = p->buffer[finished];
			p->buffer[finished] = NULL;
			p->active = next;
		}
		pdm_event(true, released, NRFX_PDM_NO_ERROR);
	} else {
		pdm_event(false, NULL, NRFX_PDM_ERROR_OVERFLOW);
	}
}

static void pdm_irq_stopped(void)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;
	uint8_t second = (~p->active) & 0x01;

	NRF_PDM0->ENABLE = 0;
	p->op_state = G1_PDM_IDLE;
	if (p->buffer[p->active]) {
		int16_t *b = p->buffer[p->active];

		p->buffer[p->active] = NULL;
		pdm_event(false, b, NRFX_PDM_NO_ERROR);
	}
	if (p->buffer[second]) {
		int16_t *b = p->buffer[second];

		p->buffer[second] = NULL;
		pdm_event(false, b, NRFX_PDM_NO_ERROR);
	}
	p->active = 0;
}

static nrfx_err_t pdm_init_default(nrfx_pdm_config_t const *cfg, nrfx_pdm_event_handler_t handler)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;

	pdm_log(G1_PDM_OP_INIT, cfg ? cfg->clock_freq : 0, 0);
	if (p->initialized) {
		return NRFX_ERROR_INVALID_STATE;
	}
	if (cfg == NULL || handler == NULL) {
		return NRFX_ERROR_INVALID_PARAM;
	}
	p->config = *cfg;
	p->handler = handler;
	p->initialized = true;
	p->powered = false;
	p->op_state = G1_PDM_IDLE;
	p->buffer[0] = p->buffer[1] = NULL;
	p->active = 0;

	NRF_PDM0->PDMCLKCTRL = cfg->clock_freq;
	NRF_PDM0->MODE = ((uint32_t)cfg->mode << G1_PDM_MODE_OPERATION_POS) |
			 ((uint32_t)cfg->edge << G1_PDM_MODE_EDGE_POS);
	NRF_PDM0->GAINL = cfg->gain_l;
	NRF_PDM0->GAINR = cfg->gain_r;
	NRF_PDM0->RATIO = cfg->ratio;
	NRF_PDM0->MCLKCONFIG = cfg->mclksrc;
	return NRFX_SUCCESS;
}

static void pdm_uninit_default(void)
{
	pdm_log(G1_PDM_OP_UNINIT, 0, 0);
	NRF_PDM0->ENABLE = 0;
	g1_fake_pdm.initialized = false;
	g1_fake_pdm.powered = false;
}

static nrfx_err_t pdm_start_default(void)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;

	pdm_log(G1_PDM_OP_START, 0, 0);
	p->gain_l_at_start = NRF_PDM0->GAINL;
	p->gain_r_at_start = NRF_PDM0->GAINR;
	if (p->op_state != G1_PDM_IDLE) {
		return p->op_state == G1_PDM_RUNNING ? NRFX_SUCCESS : NRFX_ERROR_BUSY;
	}
	p->op_state = G1_PDM_STARTING;
	pdm_event(true, NULL, NRFX_PDM_NO_ERROR);
	return NRFX_SUCCESS;
}

static nrfx_err_t pdm_buffer_set_default(int16_t *buffer, uint16_t length)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;
	uint8_t next;

	pdm_log(G1_PDM_OP_BUFFER_SET, (uintptr_t)buffer, length);
	if (!p->initialized) {
		return NRFX_ERROR_INVALID_STATE;
	}
	if (p->op_state == G1_PDM_STOPPING) {
		return NRFX_ERROR_BUSY;
	}
	if (buffer == NULL || length > NRFX_PDM_MAX_BUFFER_SIZE) {
		return NRFX_ERROR_INVALID_PARAM;
	}
	next = p->op_state == G1_PDM_STARTING ? 0 : ((~p->active) & 0x01);
	if (p->buffer[next]) {
		return NRFX_ERROR_BUSY;
	}
	p->buffer[next] = buffer;
	p->length[next] = length;
	if (!p->powered) {
		p->powered = true;
		NRF_PDM0->ENABLE = 1;
		pdm_irq_started();
	}
	return NRFX_SUCCESS;
}

static nrfx_err_t pdm_stop_default(void)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;

	pdm_log(G1_PDM_OP_STOP, 0, 0);
	if (p->op_state != G1_PDM_RUNNING) {
		if (p->op_state == G1_PDM_IDLE || p->op_state == G1_PDM_STARTING) {
			NRF_PDM0->ENABLE = 0;
			p->op_state = G1_PDM_IDLE;
			return NRFX_SUCCESS;
		}
		return NRFX_ERROR_BUSY;
	}
	p->powered = false;
	p->op_state = G1_PDM_STOPPING;
	pdm_irq_stopped();
	return NRFX_SUCCESS;
}

static void pdm_gain_set_default(NRF_PDM_Type *p_reg, nrf_pdm_gain_t gain_l, nrf_pdm_gain_t gain_r)
{
	pdm_log(G1_PDM_OP_GAIN_SET, gain_l, gain_r);
	p_reg->GAINL = gain_l;
	p_reg->GAINR = gain_r;
}

int g1_fake_pdm_capture(const int16_t *samples, size_t n)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;
	int16_t *buf = p->buffer[p->active];
	size_t len = p->length[p->active];
	size_t copy = MIN(n, len);

	if (p->op_state != G1_PDM_RUNNING || buf == NULL) {
		return -EAGAIN;
	}
	if (samples) {
		memcpy(buf, samples, copy * sizeof(int16_t));
	} else {
		copy = 0;
	}
	memset(buf + copy, 0, (len - copy) * sizeof(int16_t));
	p->captured_blocks++;
	pdm_irq_started();
	return 0;
}

int g1_fake_pdm_overflow(void)
{
	struct g1_fake_pdm *p = &g1_fake_pdm;
	uint8_t next = (~p->active) & 0x01;

	if (p->op_state != G1_PDM_RUNNING) {
		return -EAGAIN;
	}
	if (p->buffer[next]) {
		return -EBUSY;
	}
	pdm_irq_started();
	return 0;
}

static void pdm_reset(void)
{
	RESET_FAKE(nrfx_pdm_init);
	RESET_FAKE(nrfx_pdm_uninit);
	RESET_FAKE(nrfx_pdm_start);
	RESET_FAKE(nrfx_pdm_stop);
	RESET_FAKE(nrfx_pdm_buffer_set);
	RESET_FAKE(nrfx_pdm_irq_handler);
	RESET_FAKE(nrf_pdm_gain_set);
	nrfx_pdm_init_fake.custom_fake = pdm_init_default;
	nrfx_pdm_uninit_fake.custom_fake = pdm_uninit_default;
	nrfx_pdm_start_fake.custom_fake = pdm_start_default;
	nrfx_pdm_stop_fake.custom_fake = pdm_stop_default;
	nrfx_pdm_buffer_set_fake.custom_fake = pdm_buffer_set_default;
	nrf_pdm_gain_set_fake.custom_fake = pdm_gain_set_default;

	memset(&g1_fake_pdm, 0, sizeof(g1_fake_pdm));

	/* PDM reset values */
	memset(NRF_PDM0, 0, sizeof(*NRF_PDM0));
	NRF_PDM0->PDMCLKCTRL = NRF_PDM_FREQ_1032K;
	NRF_PDM0->GAINL = NRF_PDM_GAIN_DEFAULT;
	NRF_PDM0->GAINR = NRF_PDM_GAIN_DEFAULT;
}

/* ---- SPIM ---- */

DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_init, nrfx_spim_t const *,
		       nrfx_spim_config_t const *, nrfx_spim_evt_handler_t, void *);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_reconfigure, nrfx_spim_t const *,
		       nrfx_spim_config_t const *);
DEFINE_FAKE_VOID_FUNC(nrfx_spim_uninit, nrfx_spim_t const *);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_xfer, nrfx_spim_t const *,
		       nrfx_spim_xfer_desc_t const *, uint32_t);
DEFINE_FAKE_VOID_FUNC(nrfx_spim_abort, nrfx_spim_t const *);
DEFINE_FAKE_VOID_FUNC(nrfx_spim_3_irq_handler);
DEFINE_FAKE_VOID_FUNC(nrfx_spim_4_irq_handler);

__weak NRF_SPIM_Type g1_fake_nrf_spim3;
__weak NRF_SPIM_Type g1_fake_nrf_spim4;
struct g1_fake_spim g1_fake_spim;

static uint8_t spim_inst(nrfx_spim_t const *p)
{
	if (p && p->p_reg == NRF_SPIM3) {
		return 3;
	}
	if (p && p->p_reg == NRF_SPIM4) {
		return 4;
	}
	return 0xff;
}

static struct g1_spim_entry *spim_log(enum g1_fake_spim_op op, uint8_t inst, nrfx_err_t result)
{
	struct g1_spim_entry *e;

	if (g1_fake_spim.count >= G1_FAKE_SPIM_LOG_MAX) {
		g1_fake_spim.dropped++;
		return NULL;
	}
	e = &g1_fake_spim.log[g1_fake_spim.count++];
	memset(e, 0, sizeof(*e));
	e->op = op;
	e->inst = inst;
	e->result = result;
	e->t_us = k_cyc_to_us_floor32(k_cycle_get_32());
	return e;
}

void g1_fake_spim_respond(uint8_t opcode, const uint8_t *miso, size_t n)
{
	struct g1_fake_spim *s = &g1_fake_spim;
	int i;

	for (i = 0; i < s->resp_count; i++) {
		if (s->resp[i].opcode == opcode) {
			break;
		}
	}
	if (i == s->resp_count) {
		if (s->resp_count >= G1_FAKE_SPIM_RESP_MAX) {
			return;
		}
		s->resp_count++;
	}
	s->resp[i].opcode = opcode;
	s->resp[i].len = MIN(n, G1_FAKE_SPIM_RESP_LEN);
	memcpy(s->resp[i].miso, miso, s->resp[i].len);
}

int g1_fake_spim_find(enum g1_fake_spim_op op, int from)
{
	for (int i = MAX(from, 0); i < g1_fake_spim.count; i++) {
		if (g1_fake_spim.log[i].op == op) {
			return i;
		}
	}
	return -1;
}

int g1_fake_spim_find_cmd(uint8_t opcode, int from)
{
	for (int i = MAX(from, 0); i < g1_fake_spim.count; i++) {
		const struct g1_spim_entry *e = &g1_fake_spim.log[i];

		if (e->op == G1_SPIM_OP_XFER && e->len > 0 && e->head[0] == opcode) {
			return i;
		}
	}
	return -1;
}

int g1_fake_spim_xfer_index(int n)
{
	for (int i = 0; i < g1_fake_spim.count; i++) {
		if (g1_fake_spim.log[i].op == G1_SPIM_OP_XFER && n-- == 0) {
			return i;
		}
	}
	return -1;
}

void g1_fake_spim_clear_log(void)
{
	g1_fake_spim.count = 0;
	g1_fake_spim.dropped = 0;
	g1_fake_spim.xfers = 0;
	g1_fake_spim.too_long = 0;
	g1_fake_spim.pool_used = 0;
}

/* 32 MHz on SPIM4 needs its dedicated pins */
static bool spim4_32mhz_pins_ok(nrfx_spim_config_t const *c)
{
#define G1_SPIM_PIN_OK(pin, dedicated) ((pin) == NRF_SPIM_PIN_NOT_CONNECTED || (pin) == (dedicated))
	if (c->skip_gpio_cfg && c->skip_psel_cfg) {
		return true;
	}
	return G1_SPIM_PIN_OK(c->sck_pin, 8) && G1_SPIM_PIN_OK(c->mosi_pin, 9) &&
	       G1_SPIM_PIN_OK(c->miso_pin, 10) && (!c->use_hw_ss || G1_SPIM_PIN_OK(c->ss_pin, 11)) &&
	       G1_SPIM_PIN_OK(c->dcx_pin, 12);
#undef G1_SPIM_PIN_OK
}

static nrfx_err_t spim_init_default(nrfx_spim_t const *p, nrfx_spim_config_t const *cfg,
				    nrfx_spim_evt_handler_t handler, void *ctx)
{
	uint8_t inst = spim_inst(p);
	nrfx_err_t ret;

	if (inst == 0xff || cfg == NULL) {
		ret = NRFX_ERROR_INVALID_PARAM;
	} else if (g1_fake_spim.inst[inst].initialized) {
		ret = NRFX_ERROR_INVALID_STATE;
	} else if (inst == 4 && cfg->frequency == 32000000 && !spim4_32mhz_pins_ok(cfg)) {
		ret = NRFX_ERROR_INVALID_PARAM;
	} else {
		g1_fake_spim.inst[inst].initialized = true;
		g1_fake_spim.inst[inst].config = *cfg;
		g1_fake_spim.inst[inst].handler = handler;
		g1_fake_spim.inst[inst].context = ctx;
		ret = NRFX_SUCCESS;
	}
	spim_log(G1_SPIM_OP_INIT, inst, ret);
	return ret;
}

static nrfx_err_t spim_reconfigure_default(nrfx_spim_t const *p, nrfx_spim_config_t const *cfg)
{
	uint8_t inst = spim_inst(p);
	nrfx_err_t ret;

	if (inst == 0xff || cfg == NULL) {
		ret = NRFX_ERROR_INVALID_PARAM;
	} else if (!g1_fake_spim.inst[inst].initialized) {
		ret = NRFX_ERROR_INVALID_STATE;
	} else {
		g1_fake_spim.inst[inst].config = *cfg;
		ret = NRFX_SUCCESS;
	}
	spim_log(G1_SPIM_OP_RECONFIGURE, inst, ret);
	return ret;
}

static void spim_uninit_default(nrfx_spim_t const *p)
{
	uint8_t inst = spim_inst(p);

	if (inst != 0xff) {
		g1_fake_spim.inst[inst].initialized = false;
		g1_fake_spim.inst[inst].handler = NULL;
		g1_fake_spim.inst[inst].context = NULL;
	}
	spim_log(G1_SPIM_OP_UNINIT, inst, NRFX_SUCCESS);
}

static void spim_abort_default(nrfx_spim_t const *p)
{
	spim_log(G1_SPIM_OP_ABORT, spim_inst(p), NRFX_SUCCESS);
}

static uint8_t spim_miso(uint8_t opcode, size_t i)
{
	for (int r = 0; r < g1_fake_spim.resp_count; r++) {
		if (g1_fake_spim.resp[r].opcode == opcode) {
			return i < g1_fake_spim.resp[r].len ? g1_fake_spim.resp[r].miso[i]
							    : g1_fake_spim.miso_fill;
		}
	}
	return g1_fake_spim.miso_fill;
}

static nrfx_err_t spim_xfer_default(nrfx_spim_t const *p, nrfx_spim_xfer_desc_t const *d,
				    uint32_t flags)
{
	struct g1_fake_spim *s = &g1_fake_spim;
	uint8_t inst = spim_inst(p);
	nrfx_err_t ret = NRFX_SUCCESS;
	struct g1_spim_entry *e;
	size_t len = 0;
	uint8_t orc = 0xff;
	uint8_t opcode;

	s->xfers++;
	if (inst == 0xff || d == NULL) {
		ret = NRFX_ERROR_INVALID_PARAM;
	} else if (!s->inst[inst].initialized) {
		ret = NRFX_ERROR_INVALID_STATE;
	} else if (s->xfer_error != NRFX_SUCCESS) {
		if (s->xfer_error_skip > 0) {
			s->xfer_error_skip--;
		} else {
			ret = s->xfer_error;
			if (s->xfer_error_count > 0 && --s->xfer_error_count == 0) {
				s->xfer_error = NRFX_SUCCESS;
			}
		}
	}

	e = spim_log(G1_SPIM_OP_XFER, inst, ret);
	if (d == NULL) {
		return ret;
	}
	if (e) {
		e->flags = flags;
		e->tx_len = d->tx_length;
		e->rx_len = d->rx_length;
		for (size_t i = 0; i < MIN(d->tx_length, (size_t)G1_FAKE_SPIM_HEAD); i++) {
			e->head[i] = d->p_tx_buffer[i];
		}
	}
	if (ret != NRFX_SUCCESS) {
		return ret;
	}

	len = MAX(d->tx_length, d->rx_length);
	orc = s->inst[inst].config.orc;
	opcode = d->tx_length ? d->p_tx_buffer[0] : orc;
	if (len > G1_FAKE_SPIM_MAXCNT) {
		s->too_long++;
	}
	if (e) {
		e->len = len;
		if (s->pool_used + len <= sizeof(s->pool)) {
			uint8_t *mosi = &s->pool[s->pool_used];

			for (size_t i = 0; i < len; i++) {
				mosi[i] = i < d->tx_length ? d->p_tx_buffer[i] : orc;
			}
			e->mosi = mosi;
			s->pool_used += len;
		}
		for (size_t i = 0; i < MIN(len, (size_t)G1_FAKE_SPIM_HEAD); i++) {
			e->head[i] = i < d->tx_length ? d->p_tx_buffer[i] : orc;
		}
	}
	for (size_t i = 0; i < d->rx_length; i++) {
		d->p_rx_buffer[i] = spim_miso(opcode, i);
	}

	if (s->inst[inst].handler && !(flags & NRFX_SPIM_FLAG_NO_XFER_EVT_HANDLER)) {
		const nrfx_spim_evt_t evt = {
			.type = NRFX_SPIM_EVENT_DONE,
			.xfer_desc = *d,
		};

		s->inst[inst].handler(&evt, s->inst[inst].context);
	}
	return ret;
}

static void spim_reset(void)
{
	RESET_FAKE(nrfx_spim_init);
	RESET_FAKE(nrfx_spim_reconfigure);
	RESET_FAKE(nrfx_spim_uninit);
	RESET_FAKE(nrfx_spim_xfer);
	RESET_FAKE(nrfx_spim_abort);
	RESET_FAKE(nrfx_spim_3_irq_handler);
	RESET_FAKE(nrfx_spim_4_irq_handler);
	nrfx_spim_init_fake.custom_fake = spim_init_default;
	nrfx_spim_reconfigure_fake.custom_fake = spim_reconfigure_default;
	nrfx_spim_uninit_fake.custom_fake = spim_uninit_default;
	nrfx_spim_xfer_fake.custom_fake = spim_xfer_default;
	nrfx_spim_abort_fake.custom_fake = spim_abort_default;

	memset(g1_fake_spim.inst, 0, sizeof(g1_fake_spim.inst));
	g1_fake_spim.xfer_error = NRFX_SUCCESS;
	g1_fake_spim.xfer_error_skip = 0;
	g1_fake_spim.xfer_error_count = 0;
	g1_fake_spim.miso_fill = 0x00;
	g1_fake_spim.resp_count = 0;
	g1_fake_spim_clear_log();
}

/* ---- host glue ---- */

__weak void nrfx_isr(const void *irq_handler)
{
	((void (*)(void))irq_handler)();
}

static struct onoff_manager g1_fake_clock_mgr;

__weak struct onoff_manager *z_nrf_clock_control_get_onoff(clock_control_subsys_t sys)
{
	ARG_UNUSED(sys);
	return &g1_fake_clock_mgr;
}

__weak int pinctrl_lookup_state(const struct pinctrl_dev_config *config, uint8_t id,
				const struct pinctrl_state **state)
{
	for (uint8_t i = 0; i < config->state_cnt; i++) {
		if (config->states[i].id == id) {
			*state = &config->states[i];
			return 0;
		}
	}
	return -ENOENT;
}

__weak int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(pins);
	ARG_UNUSED(pin_cnt);
	ARG_UNUSED(reg);
	return 0;
}

static void fakes_nrfx_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	pdm_reset();
	spim_reset();
}

G1_UNIT_RESET_HOOK(fakes_nrfx_reset);
