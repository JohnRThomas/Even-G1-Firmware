/*
 * Zephyr's nRF PDM driver (dmic_nrfx_pdm.c) on a fake nrfx PDM. Peripheral events fire
 * synchronously: the first nrfx_pdm_buffer_set() starts the peripheral and at once requests the
 * second buffer; nrfx_pdm_stop() releases both. g1_fake_pdm_capture() fills the active buffer,
 * releases it and requests the next. A test running the driver must stop it before the next test.
 */
#ifndef G1_UNIT_AUDIO_PDM_DRIVER_H_
#define G1_UNIT_AUDIO_PDM_DRIVER_H_

#include <zephyr/fff.h>
#include <nrfx_pdm.h>

#include "harness.h"

bool pdm_driver_active(void);
int pdm_driver_queued(void);
/* stop, and free the queued blocks to the slab */
void pdm_driver_reset(void);

DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_init, nrfx_pdm_config_t const *,
      nrfx_pdm_event_handler_t);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_start);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_pdm_stop);
DECLARE_FAKE_VOID_FUNC(nrf_pdm_gain_set, NRF_PDM_Type *, nrf_pdm_gain_t, nrf_pdm_gain_t);

enum g1_fake_pdm_op {
  G1_PDM_OP_INIT = 1,
  G1_PDM_OP_UNINIT,
  G1_PDM_OP_START,
  G1_PDM_OP_BUFFER_SET,
  G1_PDM_OP_GAIN_SET,
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
    uintptr_t arg0;         /* INIT: clock_freq, BUFFER_SET: buffer, GAIN_SET: gain_l */
    uintptr_t arg1;         /* BUFFER_SET: length, GAIN_SET: gain_r */
  } log[G1_FAKE_PDM_LOG_MAX];
};

extern struct g1_fake_pdm g1_fake_pdm;

/* -EAGAIN unless running; NULL samples capture silence */
int g1_fake_pdm_capture(const int16_t *samples, size_t count);
/* index of the first logged op, -1 if none */
int g1_fake_pdm_log_find(enum g1_fake_pdm_op op);

#endif /* G1_UNIT_AUDIO_PDM_DRIVER_H_ */
