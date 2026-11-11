/* jbd_panel.c is #included so its static register cache can be reset. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "panel_support.h"

#include "panel_display/jbd_panel.c"

DEFINE_FAKE_VALUE_FUNC(int, getDeviceRole);
DEFINE_FAKE_VALUE_FUNC(int, power_for_panel);
DEFINE_FAKE_VALUE_FUNC(int, power_down_panel);
DEFINE_FAKE_VALUE_FUNC(int, opt3007_chip_init, struct opt3007_context *);
DEFINE_FAKE_VOID_FUNC(opt3007_chip_deinit);
DEFINE_FAKE_VALUE_FUNC(int, panel_related, struct jbd_panel_context *, int, void *);

struct panel_calls panel_calls;

static int power_for_panel_record(void)
{
	panel_calls.power_on_at = g1_fake_spim.count;
	return 0;
}

static int power_down_panel_record(void)
{
	panel_calls.power_down_at = g1_fake_spim.count;
	return 0;
}

static int opt3007_chip_init_record(struct opt3007_context *ctx)
{
	ARG_UNUSED(ctx);
	panel_calls.opt_init_at = g1_fake_spim.count;
	return 0;
}

static void opt3007_chip_deinit_record(void)
{
	panel_calls.opt_deinit_at = g1_fake_spim.count;
}

struct jbd_panel_context *panel_ctx(void)
{
	return &g1_state()->panel;
}

struct spi_master_ctx *panel_spi(void)
{
	return &g1_state()->spi;
}

uint8_t **panel_fb(void)
{
	return g1_state()->panel.panel_buffer;
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

	g1_fake_spim_respond(0x9f, id, sizeof(id));
}

void panel_reset_reg_cache(void)
{
	JBD_LUM_REG_VAL = 0;
	JBD_CUR_REG_VAL = 0;
}

int panel_cached_lum(void)
{
	return JBD_LUM_REG_VAL;
}

int panel_cached_cur(void)
{
	return JBD_CUR_REG_VAL;
}

int panel_xfer_count(void)
{
	int n = 0;

	for (int i = 0; i < g1_fake_spim.count; i++) {
		n += g1_fake_spim.log[i].op == G1_SPIM_OP_XFER;
	}
	return n;
}

const struct g1_spim_entry *panel_xfer(int n)
{
	int i = g1_fake_spim_xfer_index(n);

	return i < 0 ? NULL : &g1_fake_spim.log[i];
}

static void hex_short(const uint8_t *b, size_t len, char *out, size_t size)
{
	g1_hex_format(b, MIN(len, (size_t)24), out, size);
}

bool panel_xfer_check(int n, const char *hex, char *msg, size_t size)
{
	static uint8_t want[64];
	const struct g1_spim_entry *e = panel_xfer(n);
	char got_s[128], want_s[128];
	int len = g1_hex_parse(hex, want, sizeof(want));

	if (len < 0) {
		snprintf(msg, size, "bad expected hex \"%s\"", hex);
		return false;
	}
	g1_hex_format(want, len, want_s, sizeof(want_s));
	if (e == NULL) {
		snprintf(msg, size, "transfer %d: none (%d transfers), expected %s", n,
			 panel_xfer_count(), want_s);
		return false;
	}
	if (e->inst != PANEL_SPI_BUS || e->result != NRFX_SUCCESS || e->mosi == NULL) {
		snprintf(msg, size, "transfer %d: SPIM%u result 0x%x mosi %p, expected %s on SPIM4", n,
			 e->inst, e->result, (const void *)e->mosi, want_s);
		return false;
	}
	hex_short(e->mosi, e->len, got_s, sizeof(got_s));
	if (e->len != (size_t)len || memcmp(e->mosi, want, len) != 0) {
		snprintf(msg, size, "transfer %d:\n  got      (%zu) %s\n  expected (%d) %s", n, e->len,
			 got_s, len, want_s);
		return false;
	}
	return true;
}

void panel_journal_line(const struct g1_spim_entry *e, char *out, size_t size)
{
	const uint8_t *b = e->mosi;
	size_t o;

	if (e->len == 0 || b == NULL) {
		snprintf(out, size, "(nothing clocked)");
		return;
	}
	if (b[0] == SPI_WR_BUFFER && e->len >= 6) {
		uint32_t coord = (((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]) & 0x7ffff;

		snprintf(out, size, "02 x=%u y=%u bytes=%u", (unsigned int)(coord & 0x3ff),
			 (unsigned int)((coord >> 10) & 0x1ff), (unsigned int)(e->len - 6));
		return;
	}
	o = snprintf(out, size, "%02X", b[0]);
	if (e->len > 1 && o + 1 < size) {
		out[o++] = ' ';
		out[o] = '\0';
		for (size_t i = 1; i < e->len && o + 3 <= size; i++) {
			o += snprintf(out + o, size - o, "%02X", b[i]);
		}
	}
}

bool panel_journal_check(int first, const char *const *lines, int n, char *msg, size_t size)
{
	char got[64];

	for (int i = 0; i < n; i++) {
		const struct g1_spim_entry *e = panel_xfer(first + i);

		if (e == NULL) {
			snprintf(msg, size, "journal entry %d: no transfer, expected \"%s\"", i,
				 lines[i]);
			return false;
		}
		panel_journal_line(e, got, sizeof(got));
		if (strcmp(got, lines[i]) != 0) {
			snprintf(msg, size, "journal entry %d (transfer %d): got \"%s\", expected \"%s\"",
				 i, first + i, got, lines[i]);
			return false;
		}
	}
	return true;
}

static void panel_rule_before(const struct ztest_unit_test *test, void *data)
{
	struct GlassesState *gs = g1_state();

	ARG_UNUSED(test);
	ARG_UNUSED(data);

	RESET_FAKE(getDeviceRole);
	RESET_FAKE(power_for_panel);
	RESET_FAKE(power_down_panel);
	RESET_FAKE(opt3007_chip_init);
	RESET_FAKE(opt3007_chip_deinit);
	RESET_FAKE(panel_related);
	power_for_panel_fake.custom_fake = power_for_panel_record;
	power_down_panel_fake.custom_fake = power_down_panel_record;
	opt3007_chip_init_fake.custom_fake = opt3007_chip_init_record;
	opt3007_chip_deinit_fake.custom_fake = opt3007_chip_deinit_record;
	panel_calls = (struct panel_calls){-1, -1, -1, -1};

	gs->spi.is_initialized = 0;
	gs->spi.pins[0] = PANEL_SPI_BUS;
	gs->spi.pins[1] = PANEL_SPI_SCK;
	gs->spi.pins[2] = PANEL_SPI_MOSI;
	gs->spi.pins[3] = PANEL_SPI_MISO;
	gs->spi.ss = PANEL_SPI_SS;
	gs->spi.speed_mhz = PANEL_SPI_MHZ;
	spi_master_register_context(&gs->spi);
	register_jdb_panel_context(&gs->panel);
	zassert_true((uintptr_t)gs->panel.current_buffer != 5, "frame buffer allocation failed");

	gs->panel.panel_brightness_level = PANEL_DEFAULT_LEVEL;
	gs->panel.lum_gear = PANEL_DEFAULT_LUMGEAR;
	gs->panel.brightness_max = PANEL_DEFAULT_LUMGEAR;
	gs->panel.brightness_percent = 100;

	panel_reset_reg_cache();
	JDB_PANEL_ID = 0;
}

static void panel_rule_after(const struct ztest_unit_test *test, void *data)
{
	struct GlassesState *gs = g1_state();

	ARG_UNUSED(test);
	ARG_UNUSED(data);

	if (gs->panel.current_buffer) {
		free(gs->panel.current_buffer - 5);
		gs->panel.current_buffer = NULL;
	}
}

ZTEST_RULE(panel_support, panel_rule_before, panel_rule_after);
