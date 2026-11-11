/*
 * Zephyr's drivers/audio/dmic_nrfx_pdm.c (reached from ZEPHYR_BASE/include), #included for its
 * state, with the Kconfig native_sim lacks.
 */
#define CONFIG_SOC_SERIES_NRF53X        1
#define CONFIG_AUDIO_DMIC_LOG_LEVEL     1
#define CONFIG_AUDIO_DMIC_INIT_PRIORITY 80

#include <../drivers/audio/dmic_nrfx_pdm.c>

#include "pdm_driver.h"

bool pdm_driver_active(void)
{
  return dmic_nrfx_pdm_data0.active;
}

int pdm_driver_queued(void)
{
  return k_msgq_num_used_get(&dmic_nrfx_pdm_data0.rx_queue);
}

void pdm_driver_reset(void)
{
  struct dmic_nrfx_pdm_drv_data *drv_data = &dmic_nrfx_pdm_data0;
  void *buffer;

  if (drv_data->active) {
    (void)dmic_nrfx_pdm_trigger(DEVICE_DT_GET(PDM(0)), DMIC_TRIGGER_STOP);
  }
  while (k_msgq_get(&drv_data->rx_queue, &buffer, K_NO_WAIT) == 0) {
    free_buffer(drv_data, buffer);
  }
  drv_data->active = false;
  drv_data->stopping = false;
}

/* The fake nrfx PDM below the driver */

DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_init, nrfx_pdm_config_t const *,
           nrfx_pdm_event_handler_t);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_start);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_stop);
DEFINE_FAKE_VOID_FUNC(nrf_pdm_gain_set, NRF_PDM_Type *, nrf_pdm_gain_t, nrf_pdm_gain_t);
/* IRQ_CONNECT() target; the fake raises its events directly */
FAKE_VOID_FUNC(nrfx_pdm_irq_handler);

NRF_PDM_Type g1_fake_nrf_pdm0;
struct g1_fake_pdm g1_fake_pdm;

static void pdm_log(enum g1_fake_pdm_op op, uintptr_t arg0, uintptr_t arg1)
{
  if (g1_fake_pdm.log_count < G1_FAKE_PDM_LOG_MAX) {
    g1_fake_pdm.log[g1_fake_pdm.log_count].op = op;
    g1_fake_pdm.log[g1_fake_pdm.log_count].arg0 = arg0;
    g1_fake_pdm.log[g1_fake_pdm.log_count].arg1 = arg1;
    g1_fake_pdm.log_count++;
  }
}

int g1_fake_pdm_log_find(enum g1_fake_pdm_op op)
{
  for (int index = 0; index < g1_fake_pdm.log_count; index++) {
    if (g1_fake_pdm.log[index].op == op) {
      return index;
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
  }
  if (g1_fake_pdm.handler) {
    g1_fake_pdm.handler(&evt);
  }
}

/* STARTED: the DMA has latched the next buffer */
static void pdm_irq_started(void)
{
  struct g1_fake_pdm *pdm = &g1_fake_pdm;
  uint8_t finished = pdm->active;
  uint8_t next = (~pdm->active) & 0x01;

  if (pdm->buffer[next] || pdm->op_state == G1_PDM_STARTING) {
    int16_t *released = NULL;

    if (pdm->op_state == G1_PDM_STARTING) {
      pdm->op_state = G1_PDM_RUNNING;
    } else {
      released = pdm->buffer[finished];
      pdm->buffer[finished] = NULL;
      pdm->active = next;
    }
    pdm_event(true, released, NRFX_PDM_NO_ERROR);
  } else {
    pdm_event(false, NULL, NRFX_PDM_ERROR_OVERFLOW);
  }
}

/* STOPPED: releases the active buffer, then the other one */
static void pdm_irq_stopped(void)
{
  struct g1_fake_pdm *pdm = &g1_fake_pdm;
  const uint8_t order[2] = {pdm->active, (~pdm->active) & 0x01};

  NRF_PDM0->ENABLE = 0;
  pdm->op_state = G1_PDM_IDLE;
  for (int turn = 0; turn < 2; turn++) {
    int16_t *released = pdm->buffer[order[turn]];

    if (released) {
      pdm->buffer[order[turn]] = NULL;
      pdm_event(false, released, NRFX_PDM_NO_ERROR);
    }
  }
  pdm->active = 0;
}

/* the PDM is idle here: the reset, nrfx_pdm_uninit() and nrfx_pdm_stop() leave it so */
static nrfx_err_t pdm_init_default(nrfx_pdm_config_t const *cfg, nrfx_pdm_event_handler_t handler)
{
  pdm_log(G1_PDM_OP_INIT, cfg->clock_freq, 0);
  if (g1_fake_pdm.initialized) {
    return NRFX_ERROR_INVALID_STATE;
  }
  g1_fake_pdm.handler = handler;
  g1_fake_pdm.initialized = true;

  NRF_PDM0->PDMCLKCTRL = cfg->clock_freq;
  NRF_PDM0->MODE = ((uint32_t)cfg->mode << G1_PDM_MODE_OPERATION_POS) |
       ((uint32_t)cfg->edge << G1_PDM_MODE_EDGE_POS);
  NRF_PDM0->GAINL = cfg->gain_l;
  NRF_PDM0->GAINR = cfg->gain_r;
  NRF_PDM0->RATIO = cfg->ratio;
  NRF_PDM0->MCLKCONFIG = cfg->mclksrc;
  return NRFX_SUCCESS;
}

void nrfx_pdm_uninit(void)
{
  pdm_log(G1_PDM_OP_UNINIT, 0, 0);
  NRF_PDM0->ENABLE = 0;
  g1_fake_pdm.initialized = false;
  g1_fake_pdm.powered = false;
}

static nrfx_err_t pdm_start_default(void)
{
  struct g1_fake_pdm *pdm = &g1_fake_pdm;

  pdm_log(G1_PDM_OP_START, 0, 0);
  pdm->gain_l_at_start = NRF_PDM0->GAINL;
  pdm->gain_r_at_start = NRF_PDM0->GAINR;
  if (pdm->op_state != G1_PDM_IDLE) {
    return pdm->op_state == G1_PDM_RUNNING ? NRFX_SUCCESS : NRFX_ERROR_BUSY;
  }
  pdm->op_state = G1_PDM_STARTING;
  pdm_event(true, NULL, NRFX_PDM_NO_ERROR);
  return NRFX_SUCCESS;
}

nrfx_err_t nrfx_pdm_buffer_set(int16_t *buffer, uint16_t length)
{
  struct g1_fake_pdm *pdm = &g1_fake_pdm;
  uint8_t next;

  pdm_log(G1_PDM_OP_BUFFER_SET, (uintptr_t)buffer, length);
  if (!pdm->initialized) {
    return NRFX_ERROR_INVALID_STATE;
  }
  if (pdm->op_state == G1_PDM_STOPPING) {
    return NRFX_ERROR_BUSY;
  }
  if (buffer == NULL || length > NRFX_PDM_MAX_BUFFER_SIZE) {
    return NRFX_ERROR_INVALID_PARAM;
  }
  next = pdm->op_state == G1_PDM_STARTING ? 0 : ((~pdm->active) & 0x01);
  if (pdm->buffer[next]) {
    return NRFX_ERROR_BUSY;
  }
  pdm->buffer[next] = buffer;
  pdm->length[next] = length;
  if (!pdm->powered) {
    pdm->powered = true;
    NRF_PDM0->ENABLE = 1;
    pdm_irq_started();
  }
  return NRFX_SUCCESS;
}

static nrfx_err_t pdm_stop_default(void)
{
  struct g1_fake_pdm *pdm = &g1_fake_pdm;

  if (pdm->op_state != G1_PDM_RUNNING) {
    if (pdm->op_state == G1_PDM_IDLE || pdm->op_state == G1_PDM_STARTING) {
      NRF_PDM0->ENABLE = 0;
      pdm->op_state = G1_PDM_IDLE;
      return NRFX_SUCCESS;
    }
    return NRFX_ERROR_BUSY;
  }
  pdm->powered = false;
  pdm->op_state = G1_PDM_STOPPING;
  pdm_irq_stopped();
  return NRFX_SUCCESS;
}

static void pdm_gain_set_default(NRF_PDM_Type *p_reg, nrf_pdm_gain_t gain_l, nrf_pdm_gain_t gain_r)
{
  pdm_log(G1_PDM_OP_GAIN_SET, gain_l, gain_r);
  p_reg->GAINL = gain_l;
  p_reg->GAINR = gain_r;
}

int g1_fake_pdm_capture(const int16_t *samples, size_t count)
{
  struct g1_fake_pdm *pdm = &g1_fake_pdm;
  int16_t *buf = pdm->buffer[pdm->active];
  size_t len = pdm->length[pdm->active];
  size_t copy = MIN(count, len);

  if (pdm->op_state != G1_PDM_RUNNING || buf == NULL) {
    return -EAGAIN;
  }
  if (samples) {
    memcpy(buf, samples, copy * sizeof(int16_t));
  } else {
    copy = 0;
  }
  memset(buf + copy, 0, (len - copy) * sizeof(int16_t));
  pdm->captured_blocks++;
  pdm_irq_started();
  return 0;
}

static void pdm_fake_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);

  RESET_FAKE(nrfx_pdm_init);
  RESET_FAKE(nrfx_pdm_start);
  RESET_FAKE(nrfx_pdm_stop);
  RESET_FAKE(nrf_pdm_gain_set);
  nrfx_pdm_init_fake.custom_fake = pdm_init_default;
  nrfx_pdm_start_fake.custom_fake = pdm_start_default;
  nrfx_pdm_stop_fake.custom_fake = pdm_stop_default;
  nrf_pdm_gain_set_fake.custom_fake = pdm_gain_set_default;

  memset(&g1_fake_pdm, 0, sizeof(g1_fake_pdm));

  /* PDM reset values */
  memset(NRF_PDM0, 0, sizeof(*NRF_PDM0));
  NRF_PDM0->PDMCLKCTRL = NRF_PDM_FREQ_1032K;
  NRF_PDM0->GAINL = NRF_PDM_GAIN_DEFAULT;
  NRF_PDM0->GAINR = NRF_PDM_GAIN_DEFAULT;
}

G1_UNIT_RESET_HOOK(pdm_fake_reset);

/* Host glue for the driver: IRQ trampoline, clock and pins */

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
  for (uint8_t index = 0; index < config->state_cnt; index++) {
    if (config->states[index].id == id) {
      *state = &config->states[index];
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
