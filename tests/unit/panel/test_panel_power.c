/*
 * Panel bring-up, suspend, off, resume and on. Commands: 66 99 reset, 06 write enable, 01/31
 * status registers, 02 cache write, C0 offset, 71/73 mode/mirror, 36 luminance, 46 current,
 * A3/A9 enable/disable, 97 sync, 9F read ID. In deep power down (B9 FF) the panel only accepts ABh
 * and 66h/99h, so a resume starts with 66 99.
 */
#include "main.h"
#include "notify/notify.h"
#include "sensors_audio/sensors_audio.h"
#include "panel_support.h"

static const char *const VENDOR_JOURNAL[] = {
  /* init */
  "66", "99", "06", "01 10",
  "02 x=0 y=0 bytes=61440", "02 x=0 y=192 bytes=61440", "02 x=0 y=384 bytes=30720",
  "c0 00 00", "97", "c0 00 14", "97", "c0 18 00", "97", "c0 18 14", "97", "c0 0c 0a", "97",
  "71", "97", "73", "97",
  "46 0f", "31 04", "a3", "97",
  /* brightness */
  "06", "a9", "36 1b c0", "46 0f", "31 04", "a3", "97",
  /* read ID */
  "9f 00 00 00",
  /* suspend */
  "b9 ff",
};

#define N_PANEL_INIT 25
#define N_BRIGHTNESS 7
#define N_BOOT_INIT  (N_PANEL_INIT + N_BRIGHTNESS + 1)

/* jbd_panel.c's calls outside the panel files; panel_related is its default_task */
FAKE_VALUE_FUNC(int, getDeviceRole);
FAKE_VALUE_FUNC(int, power_for_panel);
FAKE_VALUE_FUNC(int, power_down_panel);
FAKE_VALUE_FUNC(int, opt3007_chip_init, struct opt3007_context *);
FAKE_VOID_FUNC(opt3007_chip_deinit);
FAKE_VALUE_FUNC(int, panel_related, struct jbd_panel_context *, int, void *);

/* SPIM log index at each fake's last call, -1 if none */
static struct panel_calls {
  int power_on_at;
  int power_down_at;
  int opt_init_at;
  int opt_deinit_at;
} panel_calls;

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

static void power_fakes_reset(void *fixture)
{
  ARG_UNUSED(fixture);
  RESET_FAKE(getDeviceRole);
  RESET_FAKE(power_for_panel);
  RESET_FAKE(power_down_panel);
  RESET_FAKE(opt3007_chip_init);
  RESET_FAKE(opt3007_chip_deinit);
  power_for_panel_fake.custom_fake = power_for_panel_record;
  power_down_panel_fake.custom_fake = power_down_panel_record;
  opt3007_chip_init_fake.custom_fake = opt3007_chip_init_record;
  opt3007_chip_deinit_fake.custom_fake = opt3007_chip_deinit_record;
  panel_calls = (struct panel_calls){-1, -1, -1, -1};
}

ZTEST_SUITE(panel_init, NULL, NULL, power_fakes_reset, NULL, NULL);
ZTEST_SUITE(panel_power, NULL, NULL, power_fakes_reset, NULL, NULL);

ZTEST(panel_init, test_boot_and_suspend_match_the_vendor_recording)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_spi_up();
  panel_answer_id();
  zassert_equal(panel->init_cb(panel), 0);
  zassert_equal(panel->suspend_cb(panel), 0);

  expect_xfer_list(VENDOR_JOURNAL, ARRAY_SIZE(VENDOR_JOURNAL));
}

ZTEST(panel_init, test_panel_init_sends_its_commands_byte_exact)
{
  panel_running();

  panel_init();

  expect_xfer_list(VENDOR_JOURNAL, N_PANEL_INIT);
  expect_clear_windows(4, 0x00);
}

ZTEST(panel_init, test_panel_init_waits_out_every_sync)
{
  panel_running();

  panel_init();

  for (int index = 0; index + 1 < g1_fake_spim.xfers; index++) {
    const struct g1_spim_entry *entry = panel_xfer(index);

    if (entry->mosi[0] == SPI_SYNC) {
      uint32_t gap = panel_xfer(index + 1)->t_us - entry->t_us;

      zassert_true(gap >= 830, "transfer %d: %u us after the sync", index + 1,
             gap);
    }
  }
}

ZTEST(panel_init, test_boot_init_reads_the_panel_id)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_spi_up();
  panel_answer_id();

  zassert_equal(panel->init_cb(panel), 0);

  zassert_equal(JDB_PANEL_ID, JBD_PANEL_DEVICE_ID);
  zassert_equal(panel->init_done, 1);
}

ZTEST(panel_init, test_boot_init_fails_when_the_panel_does_not_answer)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_spi_up();

  zassert_equal(panel->init_cb(panel), -1);

  zassert_equal(JDB_PANEL_ID, 0);
  zassert_equal(panel->init_done, 0);
  /* the commands are sent regardless */
  zassert_equal(g1_fake_spim.xfers, N_BOOT_INIT);
}

ZTEST(panel_init, test_boot_init_applies_the_stored_brightness)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_spi_up();
  panel_answer_id();
  panel->panel_brightness_level = 0x2a;

  zassert_equal(panel->init_cb(panel), 0);

  /* 0x2a: full-scale LUM, CUR 0x2a - 6 */
  expect_xfer(N_PANEL_INIT + 2, "36 1b c0");
  expect_xfer(N_PANEL_INIT + 3, "46 24");
}

static void suspended_panel(void)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_running();
  zassert_equal(panel->suspend_cb(panel), 0);
  g1_fake_spim_clear_log();
  power_fakes_reset(NULL);
}

ZTEST(panel_power, test_suspend_sends_deep_power_down_with_its_dummy_byte)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_running();
  zassert_equal(panel->suspend_cb(panel), 0);

  expect_xfers("b9 ff");
}

ZTEST(panel_power, test_suspend_releases_the_spi_then_cuts_the_rail)
{
  struct jbd_panel_context *panel = panel_ctx();
  int b9, uninit;

  panel_running();
  zassert_equal(panel->suspend_cb(panel), 0);

  b9 = g1_fake_spim_find_cmd(SPI_DEEP_POWER_DOWN);
  uninit = g1_fake_spim_find(G1_SPIM_OP_UNINIT);
  zassert_true(b9 >= 0 && uninit > b9, "B9 at %d, SPIM uninit at %d", b9, uninit);
  zassert_equal(g1_fake_spim.log[uninit].inst, 4);
  zassert_equal(power_down_panel_fake.call_count, 1);
  zassert_true(panel_calls.power_down_at > uninit, "rail off at %d, SPIM uninit at %d",
         panel_calls.power_down_at, uninit);
}

ZTEST(panel_power, test_suspend_marks_panel_and_spi_down)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_running();
  zassert_equal(panel->suspend_cb(panel), 0);

  zassert_equal(panel->init_done, 0);
  zassert_equal(panel_spi()->is_initialized, 0);
}

ZTEST(panel_power, test_suspend_stops_the_light_sensor_before_the_rail_on_the_master)
{
  struct jbd_panel_context *panel = panel_ctx();

  getDeviceRole_fake.return_val = 0;
  panel_running();
  zassert_equal(panel->suspend_cb(panel), 0);

  zassert_equal(opt3007_chip_deinit_fake.call_count, 1);
  zassert_true(panel_calls.opt_deinit_at <= panel_calls.power_down_at);
}

ZTEST(panel_power, test_suspend_leaves_the_light_sensor_alone_on_the_slave)
{
  struct jbd_panel_context *panel = panel_ctx();

  getDeviceRole_fake.return_val = 1;
  panel_running();
  zassert_equal(panel->suspend_cb(panel), 0);

  zassert_equal(opt3007_chip_deinit_fake.call_count, 0);
  zassert_equal(power_down_panel_fake.call_count, 1);
}

ZTEST(panel_power, test_off_with_the_spi_down_does_nothing)
{
  struct jbd_panel_context *panel = panel_ctx();

  zassert_equal(panel->off_cb(panel), 0);

  zassert_equal(g1_fake_spim.count, 0);
  zassert_equal(power_down_panel_fake.call_count, 0);
  zassert_equal(opt3007_chip_deinit_fake.call_count, 0);
}

ZTEST(panel_power, test_on_powers_the_rail_then_opens_the_spi_then_inits)
{
  struct jbd_panel_context *panel = panel_ctx();
  int init, first;

  zassert_equal(panel->on_cb(panel), 0);

  init = g1_fake_spim_find(G1_SPIM_OP_INIT);
  first = g1_fake_spim_find(G1_SPIM_OP_XFER);
  zassert_true(init >= 0, "SPI not opened");
  zassert_true(panel_calls.power_on_at >= 0 && panel_calls.power_on_at <= init,
         "rail on at %d, SPIM init at %d", panel_calls.power_on_at, init);
  zassert_true(first > init);
  expect_xfer_list(VENDOR_JOURNAL, N_PANEL_INIT);
  zassert_equal(panel->init_done, 1);
}

ZTEST(panel_power, test_on_starts_the_light_sensor_after_the_panel_on_the_master)
{
  struct jbd_panel_context *panel = panel_ctx();

  getDeviceRole_fake.return_val = 0;
  zassert_equal(panel->on_cb(panel), 0);

  zassert_equal(opt3007_chip_init_fake.call_count, 1);
  zassert_true(panel_calls.opt_init_at > panel_calls.power_on_at);
}

ZTEST(panel_power, test_on_leaves_the_light_sensor_alone_on_the_slave)
{
  struct jbd_panel_context *panel = panel_ctx();

  getDeviceRole_fake.return_val = 1;
  zassert_equal(panel->on_cb(panel), 0);

  zassert_equal(opt3007_chip_init_fake.call_count, 0);
  zassert_equal(g1_fake_spim.xfers, N_PANEL_INIT);
}

ZTEST(panel_power, test_on_with_the_spi_up_does_nothing)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_spi_up();
  zassert_equal(panel->on_cb(panel), 0);

  zassert_equal(g1_fake_spim.count, 0);
  zassert_equal(power_for_panel_fake.call_count, 0);
}

ZTEST(panel_power, test_resume_after_suspend_restarts_the_panel_with_its_brightness)
{
  struct jbd_panel_context *panel = panel_ctx();

  suspended_panel();

  zassert_equal(panel->resume_cb(panel), 0);

  zassert_equal(power_for_panel_fake.call_count, 1);
  zassert_true(g1_fake_spim_find(G1_SPIM_OP_INIT) >= 0, "SPI not reopened");
  expect_xfer_list(VENDOR_JOURNAL, N_PANEL_INIT + N_BRIGHTNESS);
  zassert_equal(panel->init_done, 1);
}

ZTEST(panel_power, test_resume_with_the_spi_up_reinitialises_and_reads_the_id)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_spi_up();
  panel_answer_id();

  zassert_equal(panel->resume_cb(panel), 0);

  zassert_equal(g1_fake_spim_find(G1_SPIM_OP_INIT), -1, "SPI reopened");
  zassert_equal(power_for_panel_fake.call_count, 0);
  expect_xfer_list(VENDOR_JOURNAL, N_BOOT_INIT);
  zassert_equal(panel->init_done, 1);
}

ZTEST(panel_power, test_resume_is_refused_without_the_panel_module_bit)
{
  struct jbd_panel_context *panel = panel_ctx();

  suspended_panel();
  g1_state()->module_mask &= ~LP_JBD_BIT;

  zassert_equal(panel->resume_cb(panel), -1);

  zassert_equal(g1_fake_spim.count, 0);
  zassert_equal(power_for_panel_fake.call_count, 0);
  zassert_equal(panel->init_done, 0);
}

ZTEST(panel_power, test_resume_is_refused_while_the_panel_is_on)
{
  struct jbd_panel_context *panel = panel_ctx();

  panel_running();

  zassert_equal(panel->resume_cb(panel), -1);

  zassert_equal(g1_fake_spim.count, 0);
}
