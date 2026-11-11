/*
 * Panel bring-up. Commands: 66 99 reset, 06 write enable, 01/31 status registers, 02 cache write,
 * C0 offset, 71/73 mode/mirror, 46 current, A3 enable, 97 sync.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

static const char *const VENDOR_JOURNAL[] = {
	/* init */
	"66", "99", "06", "01 10",
	"02 x=0 y=0 bytes=61440", "02 x=0 y=192 bytes=61440", "02 x=0 y=384 bytes=30720",
	"C0 0000", "97", "C0 0014", "97", "C0 1800", "97", "C0 1814", "97", "C0 0C0A", "97",
	"71", "97", "73", "97",
	"46 0F", "31 04", "A3", "97",
	/* brightness */
	"06", "A9", "36 1BC0", "46 0F", "31 04", "A3", "97",
	/* read ID */
	"9F 000000",
	/* suspend */
	"B9 FF",
};

#define N_PANEL_INIT 25
#define N_BOOT_INIT  33
#define N_JOURNAL    ARRAY_SIZE(VENDOR_JOURNAL)

ZTEST_SUITE(panel_init, NULL, NULL, NULL, NULL, NULL);

ZTEST(panel_init, test_boot_and_suspend_match_the_vendor_recording)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_spi_up();
	panel_answer_id();
	zassert_equal(c->init_cb(c), 0);
	zassert_equal(c->suspend_cb(c), 0);

	zassert_equal(panel_xfer_count(), N_JOURNAL, "%d transfers, the recording has %d",
		      panel_xfer_count(), (int)N_JOURNAL);
	expect_journal(0, VENDOR_JOURNAL, N_JOURNAL);
}

ZTEST(panel_init, test_panel_init_sends_its_commands_byte_exact)
{
	static const char *const cmds[] = {
		[0] = "66",          [1] = "99",          [2] = "06",          [3] = "01 10",
		[7] = "c0 00 00",    [8] = "97",          [9] = "c0 00 14",    [10] = "97",
		[11] = "c0 18 00",   [12] = "97",         [13] = "c0 18 14",   [14] = "97",
		[15] = "c0 0c 0a",   [16] = "97",         [17] = "71",         [18] = "97",
		[19] = "73",         [20] = "97",         [21] = "46 0f",      [22] = "31 04",
		[23] = "a3",         [24] = "97",
	};
	static const struct {
		uint8_t addr[3];
		size_t rows;
	} windows[] = {{{0x00, 0x00, 0x00}, 192}, {{0x03, 0x00, 0x00}, 192}, {{0x06, 0x00, 0x00}, 96}};

	panel_spi_up();
	panel_ctx()->spi_callbacks = panel_spi();
	panel_init();

	zassert_equal(panel_xfer_count(), N_PANEL_INIT);
	for (int i = 0; i < N_PANEL_INIT; i++) {
		if (cmds[i]) {
			expect_xfer(i, cmds[i]);
		}
	}
	for (int w = 0; w < 3; w++) {
		const struct g1_spim_entry *e = panel_xfer(4 + w);
		size_t n = windows[w].rows * JBD_ROW_BYTES;

		zassert_equal(e->head[0], SPI_WR_BUFFER);
		zassert_mem_equal(&e->head[1], windows[w].addr, 3, "window %d address", w);
		zassert_equal(e->head[4], 0xff, "window %d dummy byte", w);
		zassert_equal(e->len, 5 + n + 1, "window %d length", w);
		for (size_t i = 0; i < n; i++) {
			zassert_equal(e->mosi[5 + i], 0x00, "window %d pixel byte %zu", w, i);
		}
	}
}

ZTEST(panel_init, test_panel_init_waits_out_every_sync)
{
	panel_spi_up();
	panel_ctx()->spi_callbacks = panel_spi();
	panel_init();

	for (int i = 0; i + 1 < panel_xfer_count(); i++) {
		const struct g1_spim_entry *e = panel_xfer(i);

		if (e->head[0] == SPI_SYNC) {
			uint32_t gap = panel_xfer(i + 1)->t_us - e->t_us;

			zassert_true(gap >= 830, "transfer %d: %u us after the sync", i + 1, gap);
		}
	}
}

ZTEST(panel_init, test_boot_init_reads_the_panel_id)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_spi_up();
	panel_answer_id();

	zassert_equal(c->init_cb(c), 0);

	zassert_equal(JDB_PANEL_ID, JBD_PANEL_DEVICE_ID);
	zassert_equal(c->init_done, 1);
}

ZTEST(panel_init, test_boot_init_fails_when_the_panel_does_not_answer)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_spi_up();

	zassert_equal(c->init_cb(c), -1);

	zassert_equal(JDB_PANEL_ID, 0);
	zassert_equal(c->init_done, 0);
	/* the commands are sent regardless */
	zassert_equal(panel_xfer_count(), N_BOOT_INIT);
}

ZTEST(panel_init, test_boot_init_applies_the_stored_brightness)
{
	struct jbd_panel_context *c = panel_ctx();

	panel_spi_up();
	panel_answer_id();
	c->panel_brightness_level = 0x2a;

	zassert_equal(c->init_cb(c), 0);

	/* 0x2a: full-scale LUM, CUR 0x2a - 6 */
	expect_xfer(N_PANEL_INIT + 2, "36 1b c0");
	expect_xfer(N_PANEL_INIT + 3, "46 24");
}
