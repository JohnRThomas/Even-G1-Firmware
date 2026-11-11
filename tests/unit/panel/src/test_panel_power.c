/*
 * Panel suspend, off, resume and on. In deep power down (B9 FF) the panel only accepts ABh and
 * 66h/99h, so a resume starts with 66 99.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

static const char *const VENDOR_INIT[] = {
	"66", "99", "06", "01 10",
	"02 x=0 y=0 bytes=61440", "02 x=0 y=192 bytes=61440", "02 x=0 y=384 bytes=30720",
	"C0 0000", "97", "C0 0014", "97", "C0 1800", "97", "C0 1814", "97", "C0 0C0A", "97",
	"71", "97", "73", "97",
	"46 0F", "31 04", "A3", "97",
	/* brightness */
	"06", "A9", "36 1BC0", "46 0F", "31 04", "A3", "97",
	/* read ID */
	"9F 000000",
};

#define N_PANEL_INIT 25
#define N_BRIGHTNESS 7

ZTEST_SUITE(panel_power, NULL, NULL, NULL, NULL, NULL);

static void suspended_panel(void)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_running();
	zassert_equal(c->suspend_cb(c), 0);
	g1_fake_spim_clear_log();
	panel_calls = (struct panel_calls){-1, -1, -1, -1};
	power_for_panel_fake.call_count = 0;
	power_down_panel_fake.call_count = 0;
	opt3007_chip_init_fake.call_count = 0;
	opt3007_chip_deinit_fake.call_count = 0;
}

ZTEST(panel_power, test_suspend_sends_deep_power_down_with_its_dummy_byte)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_running();
	zassert_equal(c->suspend_cb(c), 0);

	zassert_equal(panel_xfer_count(), 1);
	expect_xfer(0, "b9 ff");
}

ZTEST(panel_power, test_suspend_releases_the_spi_then_cuts_the_rail)
{
	struct jbd_panel_context *c = panel_ctx();
	int b9, uninit;

	panel_running();
	zassert_equal(c->suspend_cb(c), 0);

	b9 = g1_fake_spim_find_cmd(SPI_DEEP_POWER_DOWN, 0);
	uninit = g1_fake_spim_find(G1_SPIM_OP_UNINIT, 0);
	zassert_true(b9 >= 0 && uninit > b9, "B9 at %d, SPIM uninit at %d", b9, uninit);
	zassert_equal(g1_fake_spim.log[uninit].inst, 4);
	zassert_equal(power_down_panel_fake.call_count, 1);
	zassert_true(panel_calls.power_down_at > uninit, "rail off at %d, SPIM uninit at %d",
		     panel_calls.power_down_at, uninit);
}

ZTEST(panel_power, test_suspend_marks_panel_and_spi_down)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_running();
	zassert_equal(c->suspend_cb(c), 0);

	zassert_equal(c->init_done, 0);
	zassert_equal(panel_spi()->is_initialized, 0);
}

ZTEST(panel_power, test_suspend_stops_the_light_sensor_before_the_rail_on_the_master)
{
	struct jbd_panel_context *c = panel_ctx();

	getDeviceRole_fake.return_val = 0;
	panel_running();
	zassert_equal(c->suspend_cb(c), 0);

	zassert_equal(opt3007_chip_deinit_fake.call_count, 1);
	zassert_true(panel_calls.opt_deinit_at <= panel_calls.power_down_at);
}

ZTEST(panel_power, test_suspend_leaves_the_light_sensor_alone_on_the_slave)
{
	struct jbd_panel_context *c = panel_ctx();

	getDeviceRole_fake.return_val = 1;
	panel_running();
	zassert_equal(c->suspend_cb(c), 0);

	zassert_equal(opt3007_chip_deinit_fake.call_count, 0);
	zassert_equal(power_down_panel_fake.call_count, 1);
}

ZTEST(panel_power, test_off_with_the_spi_down_does_nothing)
{
	struct jbd_panel_context *c = panel_ctx();

	zassert_equal(c->off_cb(c), 0);

	zassert_equal(g1_fake_spim.count, 0);
	zassert_equal(power_down_panel_fake.call_count, 0);
	zassert_equal(opt3007_chip_deinit_fake.call_count, 0);
}

ZTEST(panel_power, test_on_powers_the_rail_then_opens_the_spi_then_inits)
{
	struct jbd_panel_context *c = panel_ctx();
	int init, first;

	zassert_equal(c->on_cb(c), 0);

	init = g1_fake_spim_find(G1_SPIM_OP_INIT, 0);
	first = g1_fake_spim_find(G1_SPIM_OP_XFER, 0);
	zassert_true(init >= 0, "SPI not opened");
	zassert_true(panel_calls.power_on_at >= 0 && panel_calls.power_on_at <= init,
		     "rail on at %d, SPIM init at %d", panel_calls.power_on_at, init);
	zassert_true(first > init);
	zassert_equal(panel_xfer_count(), N_PANEL_INIT);
	expect_journal(0, VENDOR_INIT, N_PANEL_INIT);
	zassert_equal(c->init_done, 1);
}

ZTEST(panel_power, test_on_starts_the_light_sensor_after_the_panel_on_the_master)
{
	struct jbd_panel_context *c = panel_ctx();

	getDeviceRole_fake.return_val = 0;
	zassert_equal(c->on_cb(c), 0);

	zassert_equal(opt3007_chip_init_fake.call_count, 1);
	zassert_true(panel_calls.opt_init_at > panel_calls.power_on_at);
}

ZTEST(panel_power, test_on_leaves_the_light_sensor_alone_on_the_slave)
{
	struct jbd_panel_context *c = panel_ctx();

	getDeviceRole_fake.return_val = 1;
	zassert_equal(c->on_cb(c), 0);

	zassert_equal(opt3007_chip_init_fake.call_count, 0);
	zassert_equal(panel_xfer_count(), N_PANEL_INIT);
}

ZTEST(panel_power, test_on_with_the_spi_up_does_nothing)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_spi_up();
	zassert_equal(c->on_cb(c), 0);

	zassert_equal(g1_fake_spim.count, 0);
	zassert_equal(power_for_panel_fake.call_count, 0);
}

ZTEST(panel_power, test_resume_after_suspend_restarts_the_panel_with_its_brightness)
{
	struct jbd_panel_context *c = panel_ctx();

	suspended_panel();

	zassert_equal(c->resume_cb(c), 0);

	zassert_equal(power_for_panel_fake.call_count, 1);
	zassert_true(g1_fake_spim_find(G1_SPIM_OP_INIT, 0) >= 0, "SPI not reopened");
	zassert_equal(panel_xfer_count(), N_PANEL_INIT + N_BRIGHTNESS);
	expect_journal(0, VENDOR_INIT, N_PANEL_INIT + N_BRIGHTNESS);
	zassert_equal(c->init_done, 1);
}

ZTEST(panel_power, test_resume_with_the_spi_up_reinitialises_and_reads_the_id)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_spi_up();
	panel_answer_id();

	zassert_equal(c->resume_cb(c), 0);

	zassert_equal(g1_fake_spim_find(G1_SPIM_OP_INIT, 0), -1, "SPI reopened");
	zassert_equal(power_for_panel_fake.call_count, 0);
	zassert_equal(panel_xfer_count(), N_PANEL_INIT + N_BRIGHTNESS + 1);
	expect_journal(0, VENDOR_INIT, N_PANEL_INIT + N_BRIGHTNESS + 1);
	zassert_equal(c->init_done, 1);
}

ZTEST(panel_power, test_resume_is_refused_without_the_panel_module_bit)
{
	struct jbd_panel_context *c = panel_ctx();

	suspended_panel();
	g1_state()->module_mask &= ~LP_JBD_BIT;

	zassert_equal(c->resume_cb(c), -1);

	zassert_equal(g1_fake_spim.count, 0);
	zassert_equal(power_for_panel_fake.call_count, 0);
	zassert_equal(c->init_done, 0);
}

ZTEST(panel_power, test_resume_is_refused_while_the_panel_is_on)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_running();

	zassert_equal(c->resume_cb(c), -1);

	zassert_equal(g1_fake_spim.count, 0);
}
