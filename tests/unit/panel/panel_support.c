#include <stdlib.h>

#include "panel_support.h"
#include "spim_spis/spim.h"

/* The fake nrfx SPIM under spim.c */

DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_init, nrfx_spim_t const *,
           nrfx_spim_config_t const *, nrfx_spim_evt_handler_t, void *);
DEFINE_FAKE_VOID_FUNC(nrfx_spim_uninit, nrfx_spim_t const *);
DEFINE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_xfer, nrfx_spim_t const *,
           nrfx_spim_xfer_desc_t const *, uint32_t);
FAKE_VOID_FUNC(nrfx_spim_3_irq_handler);
FAKE_VOID_FUNC(nrfx_spim_4_irq_handler);

NRF_SPIM_Type g1_fake_nrf_spim3;
NRF_SPIM_Type g1_fake_nrf_spim4;
struct g1_fake_spim g1_fake_spim;

static uint8_t spim_inst(nrfx_spim_t const *spim)
{
  zassert_true(spim->p_reg == NRF_SPIM3 || spim->p_reg == NRF_SPIM4, "not SPIM3 or SPIM4");
  return spim->p_reg == NRF_SPIM3 ? 3 : 4;
}

static struct g1_spim_entry *spim_log(enum g1_fake_spim_op op, uint8_t inst, nrfx_err_t result)
{
  struct g1_spim_entry *entry;

  zassert_true(g1_fake_spim.count < G1_FAKE_SPIM_LOG_MAX, "SPIM log full");
  entry = &g1_fake_spim.log[g1_fake_spim.count++];
  memset(entry, 0, sizeof(*entry));
  entry->op = op;
  entry->inst = inst;
  entry->result = result;
  entry->t_us = k_cyc_to_us_floor32(k_cycle_get_32());
  return entry;
}

int g1_fake_spim_find(enum g1_fake_spim_op op)
{
  for (int index = 0; index < g1_fake_spim.count; index++) {
    if (g1_fake_spim.log[index].op == op) {
      return index;
    }
  }
  return -1;
}

int g1_fake_spim_find_cmd(uint8_t opcode)
{
  for (int index = 0; index < g1_fake_spim.count; index++) {
    const struct g1_spim_entry *entry = &g1_fake_spim.log[index];

    if (entry->op == G1_SPIM_OP_XFER && entry->len > 0 && entry->mosi[0] == opcode) {
      return index;
    }
  }
  return -1;
}

void g1_fake_spim_clear_log(void)
{
  g1_fake_spim.count = 0;
  g1_fake_spim.xfers = 0;
  g1_fake_spim.too_long = 0;
  g1_fake_spim.pool_used = 0;
}

static nrfx_err_t spim_init_default(nrfx_spim_t const *spim, nrfx_spim_config_t const *config,
            nrfx_spim_evt_handler_t handler, void *context)
{
  uint8_t inst = spim_inst(spim);
  nrfx_err_t ret = NRFX_SUCCESS;

  if (g1_fake_spim.inst[inst].initialized) {
    ret = NRFX_ERROR_INVALID_STATE;
  } else {
    g1_fake_spim.inst[inst].initialized = true;
    g1_fake_spim.inst[inst].config = *config;
    g1_fake_spim.inst[inst].handler = handler;
    g1_fake_spim.inst[inst].context = context;
  }
  spim_log(G1_SPIM_OP_INIT, inst, ret);
  return ret;
}

static void spim_uninit_default(nrfx_spim_t const *spim)
{
  uint8_t inst = spim_inst(spim);

  g1_fake_spim.inst[inst].initialized = false;
  spim_log(G1_SPIM_OP_UNINIT, inst, NRFX_SUCCESS);
}

static nrfx_err_t spim_xfer_default(nrfx_spim_t const *spim, nrfx_spim_xfer_desc_t const *desc,
            uint32_t flags)
{
  struct g1_fake_spim *fake = &g1_fake_spim;
  uint8_t inst = spim_inst(spim);
  size_t len = MAX(desc->tx_length, desc->rx_length);
  nrfx_err_t ret = NRFX_SUCCESS;
  struct g1_spim_entry *entry;
  uint8_t *mosi;

  if (!fake->inst[inst].initialized) {
    ret = NRFX_ERROR_INVALID_STATE;
  } else if (fake->xfer_error != NRFX_SUCCESS && fake->xfer_error_skip > 0) {
    fake->xfer_error_skip--;
  } else if (fake->xfer_error != NRFX_SUCCESS) {
    ret = fake->xfer_error;
    if (fake->xfer_error_count > 0 && --fake->xfer_error_count == 0) {
      fake->xfer_error = NRFX_SUCCESS;
    }
  }
  entry = spim_log(G1_SPIM_OP_XFER, inst, ret);
  fake->xfers++;
  entry->flags = flags;
  entry->tx_len = desc->tx_length;
  entry->rx_len = desc->rx_length;
  if (ret != NRFX_SUCCESS) {
    return ret;
  }

  zassert_true(fake->pool_used + len <= sizeof(fake->pool), "SPIM pool full");
  mosi = &fake->pool[fake->pool_used];
  fake->pool_used += len;
  memcpy(mosi, desc->p_tx_buffer, desc->tx_length);
  memset(mosi + desc->tx_length, fake->inst[inst].config.orc, len - desc->tx_length);
  entry->mosi = mosi;
  entry->len = len;
  if (len > 0xffff) {
    fake->too_long++;
  }
  for (size_t offset = 0; offset < desc->rx_length; offset++) {
    bool answered = mosi[0] == fake->miso_opcode && offset < fake->miso_len;

    desc->p_rx_buffer[offset] = answered ? fake->miso[offset] : 0;
  }

  if (fake->inst[inst].handler && !(flags & NRFX_SPIM_FLAG_NO_XFER_EVT_HANDLER)) {
    const nrfx_spim_evt_t event = {.type = NRFX_SPIM_EVENT_DONE, .xfer_desc = *desc};

    fake->inst[inst].handler(&event, fake->inst[inst].context);
  }
  return ret;
}

static void spim_fake_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);

  RESET_FAKE(nrfx_spim_init);
  RESET_FAKE(nrfx_spim_uninit);
  RESET_FAKE(nrfx_spim_xfer);
  nrfx_spim_init_fake.custom_fake = spim_init_default;
  nrfx_spim_uninit_fake.custom_fake = spim_uninit_default;
  nrfx_spim_xfer_fake.custom_fake = spim_xfer_default;

  memset(g1_fake_spim.inst, 0, sizeof(g1_fake_spim.inst));
  g1_fake_spim.xfer_error = NRFX_SUCCESS;
  g1_fake_spim.xfer_error_skip = 0;
  g1_fake_spim.xfer_error_count = 0;
  g1_fake_spim.miso_len = 0;
  g1_fake_spim_clear_log();
}

G1_UNIT_RESET_HOOK(spim_fake_reset);

struct jbd_panel_context *panel_ctx(void)
{
  return &g1_state()->panel;
}

struct spi_master_ctx *panel_spi(void)
{
  return &g1_state()->spi;
}

uint8_t *panel_frame(void)
{
  return g1_state()->panel.current_buffer;
}

void panel_spi_up(void)
{
  struct spi_master_ctx *spi = panel_spi();

  zassert_equal(spi->init_cb(spi), 0);
  zassert_equal(spi->is_initialized, 1, "SPI init failed");
  g1_fake_spim_clear_log();
}

void panel_running(void)
{
  panel_spi_up();
  panel_ctx()->spi_callbacks = panel_spi();
  panel_ctx()->init_done = 1;
}

void panel_answer_id(void)
{
  /* JBD013 ID 0xBD 0x4010, after one high-Z byte */
  static const uint8_t id[] = {0x00, 0xbd, 0x40, 0x10};

  g1_fake_spim.miso_opcode = SPI_RD_ID;
  g1_fake_spim.miso = id;
  g1_fake_spim.miso_len = sizeof(id);
}

const struct g1_spim_entry *panel_xfer(int index)
{
  for (int position = 0; position < g1_fake_spim.count; position++) {
    if (g1_fake_spim.log[position].op == G1_SPIM_OP_XFER && index-- == 0) {
      return &g1_fake_spim.log[position];
    }
  }
  return NULL;
}

bool panel_xfer_check(int index, const char *hex, char *msg, size_t msg_size)
{
  const struct g1_spim_entry *entry = panel_xfer(index);
  unsigned int col, row, pixel_bytes;
  uint32_t addr;
  int used;

  if (entry == NULL) {
    snprintf(msg, msg_size, "transfer %d: none (%d transfers), expected %s", index,
       g1_fake_spim.xfers, hex);
    return false;
  }
  if (entry->inst != 4 || entry->result != NRFX_SUCCESS || entry->mosi == NULL) {
    snprintf(msg, msg_size,
       "transfer %d: SPIM%u result 0x%x mosi %p, expected %s on SPIM4", index,
       entry->inst, entry->result, (const void *)entry->mosi, hex);
    return false;
  }
  if (sscanf(hex, "02 x=%u y=%u bytes=%u", &col, &row, &pixel_bytes) != 3) {
    used = snprintf(msg, msg_size, "transfer %d: ", index);
    return g1_hex_check(entry->mosi, entry->len, hex, msg + used, msg_size - used);
  }
  /* the cache address is (y << 10) | x */
  addr = sys_get_be24(&entry->mosi[1]) & 0x7ffff;
  snprintf(msg, msg_size, "transfer %d: %02x, %zu bytes at x=%u y=%u, expected %s", index,
     entry->mosi[0], entry->len, addr & 0x3ff, addr >> 10, hex);
  return entry->mosi[0] == SPI_WR_BUFFER && entry->len == pixel_bytes + 6 &&
         (addr & 0x3ff) == col && addr >> 10 == row;
}

bool panel_xfers_check(const char *const *hex, int count, char *msg, size_t msg_size)
{
  if (g1_fake_spim.xfers != count) {
    snprintf(msg, msg_size, "%d transfers, expected %d", g1_fake_spim.xfers, count);
    return false;
  }
  for (int index = 0; index < count; index++) {
    if (!panel_xfer_check(index, hex[index], msg, msg_size)) {
      return false;
    }
  }
  return true;
}

const struct g1_spim_entry *expect_window(int index, const char *header)
{
  const struct g1_spim_entry *entry = panel_xfer(index);

  zassert_not_null(entry, "no transfer %d", index);
  zassert_not_null(entry->mosi, "transfer %d failed", index);
  expect_bytes_msg(entry->mosi, 5, header, "transfer %d header", index);
  return entry;
}

void expect_fill_window(int index, const char *header, uint8_t fill, size_t pixel_bytes)
{
  const struct g1_spim_entry *entry = expect_window(index, header);

  zassert_equal(entry->len, 5 + pixel_bytes + 1, "transfer %d: %zu bytes", index, entry->len);
  expect_fill(entry->mosi + 5, pixel_bytes, fill, header);
}

void expect_clear_windows(int first, uint8_t fill)
{
  expect_fill_window(first, "02 00 00 00 ff", fill, 192 * JBD_ROW_BYTES);
  expect_fill_window(first + 1, "02 03 00 00 ff", fill, 192 * JBD_ROW_BYTES);
  expect_fill_window(first + 2, "02 06 00 00 ff", fill, 96 * JBD_ROW_BYTES);
}

static void panel_rule_before(const struct ztest_unit_test *test, void *data)
{
  struct GlassesState *state = g1_state();

  ARG_UNUSED(test);
  ARG_UNUSED(data);

  /* as init_context(): SPIM4, SCK 8, MOSI 9, MISO 10, CSB 11, 32 MHz */
  state->spi.bus = 4;
  state->spi.sck = 8;
  state->spi.mosi = 9;
  state->spi.miso = 10;
  state->spi.ss = 11;
  state->spi.speed_mhz = 32;
  spi_master_register_context(&state->spi);
  register_jdb_panel_context(&state->panel);
  zassert_true((uintptr_t)state->panel.current_buffer != 5, "frame buffer allocation failed");

  state->panel.panel_brightness_level = PANEL_DEFAULT_LEVEL;
  state->panel.lum_gear = PANEL_DEFAULT_LUMGEAR;
  state->panel.brightness_max = PANEL_DEFAULT_LUMGEAR;
  state->panel.brightness_percent = 100;
  JDB_PANEL_ID = 0;
}

static void panel_rule_after(const struct ztest_unit_test *test, void *data)
{
  struct GlassesState *state = g1_state();

  ARG_UNUSED(test);
  ARG_UNUSED(data);

  if (state->panel.current_buffer) {
    free(state->panel.current_buffer - 5);
    state->panel.current_buffer = NULL;
  }
}

ZTEST_RULE(panel_support, panel_rule_before, panel_rule_after);
