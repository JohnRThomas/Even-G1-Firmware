/*
 * SPI master callbacks on the nrfx SPIM fakes. SPIM4 must block: callers reuse buffers on return.
 * ss_duration 2 = 31.25 ns CSB setup at 64 MHz.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

ZTEST_SUITE(spim, NULL, NULL, NULL, NULL, NULL);

ZTEST(spim, test_init_opens_spim4_on_the_panel_pins_at_32mhz)
{
	struct spi_master_ctx *spi = panel_spi();
	const nrfx_spim_config_t *cfg = &g1_fake_spim.inst[4].config;
	int i;

	zassert_equal(spi->init_cb(spi), 0);

	i = g1_fake_spim_find(G1_SPIM_OP_INIT, 0);
	zassert_true(i >= 0, "no nrfx_spim_init()");
	zassert_equal(g1_fake_spim.log[i].inst, 4);
	zassert_equal(g1_fake_spim.log[i].result, NRFX_SUCCESS, "nrfx refused the configuration");
	zassert_equal(cfg->sck_pin, 8);
	zassert_equal(cfg->mosi_pin, 9);
	zassert_equal(cfg->miso_pin, 10);
	zassert_equal(cfg->ss_pin, 11);
	zassert_false(cfg->ss_active_high, "CSB is active low");
	zassert_equal(cfg->frequency, 32000000);
	zassert_true(cfg->mode == NRF_SPIM_MODE_0 || cfg->mode == NRF_SPIM_MODE_3);
	zassert_equal(cfg->bit_order, NRF_SPIM_BIT_ORDER_MSB_FIRST);
	zassert_equal(cfg->dcx_pin, NRF_SPIM_PIN_NOT_CONNECTED);
	zassert_equal(cfg->ss_duration, 2);
	zassert_equal(spi->is_initialized, 1);
}

ZTEST(spim, test_spim4_transfers_are_blocking)
{
	struct spi_master_ctx *spi = panel_spi();

	zassert_equal(spi->init_cb(spi), 0);
	zassert_is_null(g1_fake_spim.inst[4].handler);
}

ZTEST(spim, test_second_init_leaves_the_driver_alone)
{
	struct spi_master_ctx *spi = panel_spi();

	zassert_equal(spi->init_cb(spi), 0);
	zassert_equal(spi->init_cb(spi), 0);

	zassert_equal(nrfx_spim_init_fake.call_count, 1);
}

ZTEST(spim, test_init_on_an_unknown_bus_touches_no_instance)
{
	struct spi_master_ctx *spi = panel_spi();

	spi->bus = 2;
	(void)spi->init_cb(spi);

	zassert_equal(nrfx_spim_init_fake.call_count, 0);
	zassert_equal(spi->is_initialized, 0);
}

ZTEST(spim, test_xfer_before_init_clocks_nothing)
{
	struct spi_master_ctx *spi = panel_spi();
	uint8_t tx[1] = {0x97};
	uint8_t rx[1];

	zassert_equal(spi->xfer_cb(spi, tx, 1, rx, 1), -1);
	zassert_equal(nrfx_spim_xfer_fake.call_count, 0);
}

ZTEST(spim, test_xfer_is_one_transaction_with_both_buffers)
{
	struct spi_master_ctx *spi = panel_spi();
	uint8_t tx[3] = {0x36, 0x1b, 0xc0};
	uint8_t rx[1] = {0x55};
	const struct g1_spim_entry *e;

	panel_spi_up();
	zassert_equal(spi->xfer_cb(spi, tx, sizeof(tx), rx, sizeof(rx)), 0);

	zassert_equal(panel_xfer_count(), 1);
	e = panel_xfer(0);
	zassert_equal(e->inst, 4);
	zassert_equal(e->tx_len, 3);
	zassert_equal(e->rx_len, 1);
	zassert_equal(e->flags, 0);
	expect_xfer(0, "36 1b c0");
	/* the fake's MISO fill byte */
	zassert_equal(rx[0], 0x00);
}

ZTEST(spim, test_xfer_returns_the_received_bytes)
{
	struct spi_master_ctx *spi = panel_spi();
	uint32_t tx = 0x9f;
	uint8_t rx[4] = {0};

	panel_spi_up();
	panel_answer_id();
	zassert_equal(spi->xfer_cb(spi, (uint8_t *)&tx, 4, rx, 4), 0);

	zassert_mem_equal(rx, ((uint8_t[]){0x00, 0xbd, 0x40, 0x10}), 4);
}

ZTEST(spim, test_xfer_error_is_reported_as_minus_one)
{
	struct spi_master_ctx *spi = panel_spi();
	uint8_t tx[1] = {0x97};
	uint8_t rx[1];

	panel_spi_up();
	g1_fake_spim.xfer_error = NRFX_ERROR_BUSY;

	zassert_equal(spi->xfer_cb(spi, tx, 1, rx, 1), -1);
}

ZTEST(spim, test_deinit_releases_spim4)
{
	struct spi_master_ctx *spi = panel_spi();
	int i;

	panel_spi_up();
	zassert_equal(spi->deinit_cb(spi), 0);

	i = g1_fake_spim_find(G1_SPIM_OP_UNINIT, 0);
	zassert_true(i >= 0, "no nrfx_spim_uninit()");
	zassert_equal(g1_fake_spim.log[i].inst, 4);
	zassert_false(g1_fake_spim.inst[4].initialized);
	zassert_equal(spi->is_initialized, 0);
}

ZTEST(spim, test_deinit_when_not_initialised_does_nothing)
{
	struct spi_master_ctx *spi = panel_spi();

	zassert_equal(spi->deinit_cb(spi), 0);
	zassert_equal(nrfx_spim_uninit_fake.call_count, 0);
}

ZTEST(spim, test_reinit_after_deinit_opens_spim4_again)
{
	struct spi_master_ctx *spi = panel_spi();

	panel_spi_up();
	zassert_equal(spi->deinit_cb(spi), 0);
	zassert_equal(spi->init_cb(spi), 0);

	zassert_equal(nrfx_spim_init_fake.call_count, 2);
	zassert_equal(g1_fake_spim.log[g1_fake_spim_find(G1_SPIM_OP_INIT, 0)].result, NRFX_SUCCESS);
	zassert_equal(spi->is_initialized, 1);
}

ZTEST(spim, test_bus_3_signals_transfer_done_through_its_handler)
{
	struct spi_master_ctx *spi = panel_spi();
	uint8_t tx[1] = {0x97};
	uint8_t rx[1];

	spi->bus = 3;
	zassert_equal(spi->init_cb(spi), 0);
	zassert_true(g1_fake_spim.inst[3].initialized);
	zassert_not_null(g1_fake_spim.inst[3].handler);
	zassert_equal_ptr(g1_fake_spim.inst[3].context, spi);
	spi->xfer_done = 0;

	zassert_equal(spi->xfer_cb(spi, tx, 1, rx, 1), 0);

	zassert_equal(spi->xfer_done, 1);
}
