/* nfc/eeprom_st25dv.c and nfc/st25dv.c: the ST25DV driver on the emulated tag. */
#include <zephyr/drivers/eeprom.h>

#include "drivers.h"
#include "harness.h"
#include "st25dv_emul.h"

#include "nfc/st25dv.h"

static const struct device *dev;

static void st25dv_before(void *fixture) {
  ARG_UNUSED(fixture);
  dev = eeprom_st25dv_init();
  zassert_not_null(dev);
}

ZTEST_SUITE(nfc_st25dv, NULL, NULL, st25dv_before, NULL, NULL);

static void open_session(void) {
  static const struct st25dv_passwd zero;

  zassert_ok(st25dv_present_i2c_password(dev, &zero));
  zassert_true(st25dv_emul_session_open());
}

static const struct st25dv_emul_xfer *last_xfer(void) {
  zassert_true(st25dv_emul_xfers() > 0, "no transfer");
  return st25dv_emul_xfer(st25dv_emul_xfers() - 1);
}

/* the transfer went to I2C address addr and wrote exactly the bytes of hex (40 are logged) */
static void expect_wr(const struct st25dv_emul_xfer *xfer, uint8_t addr, const char *hex) {
  zassert_equal(xfer->addr, addr, "device address 0x%02x", xfer->addr);
  expect_bytes(xfer->wr, MIN(xfer->wr_len, sizeof(xfer->wr)), hex);
}

/* ---- device ---- */

ZTEST(nfc_st25dv, test_init_returns_the_tag_device_on_i2c1) {
  const struct eeprom_st25dv_config *cfg = dev->config;

  zassert_equal_ptr(dev, &global_st25dv_context);
  zassert_equal_ptr(cfg->bus, G1_I2C1);
  zassert_equal(cfg->addr, 0x53);
  zassert_not_null(dev->api);
  zassert_not_null(dev->data);
}

ZTEST(nfc_st25dv, test_init_with_the_bus_not_ready_returns_null) {
  g1_device_set_ready(G1_I2C1, false);

  zassert_is_null(eeprom_st25dv_init());
}

/* the 04KC has 512 bytes of user memory */
ZTEST(nfc_st25dv, test_eeprom_size_is_the_user_memory_of_the_tag) {
  zassert_equal(eeprom_get_size(dev), 512, "size %zu", eeprom_get_size(dev));
}

/* ---- addressing ---- */

ZTEST(nfc_st25dv, test_user_read_sends_the_16_bit_address_msb_first_to_0x53) {
  uint8_t buf[3];

  memcpy(&st25dv_emul_user_mem()[0x1a5], "\x11\x22\x33", 3);

  zassert_ok(eeprom_st25dv_read(dev, 0x01a5, buf, sizeof(buf)));
  expect_wr(last_xfer(), 0x53, "01 a5");
  zassert_equal(last_xfer()->rd_len, 3);
  zassert_mem_equal(buf, "\x11\x22\x33", 3);
}

ZTEST(nfc_st25dv, test_user_write_is_one_transfer_of_address_and_data) {
  zassert_ok(eeprom_st25dv_write(dev, 0x0123, "\xaa\xbb", 2));
  zassert_equal(st25dv_emul_xfers(), 1);
  expect_wr(last_xfer(), 0x53, "01 23 aa bb");
  zassert_equal(last_xfer()->rd_len, 0);
  zassert_mem_equal(&st25dv_emul_user_mem()[0x123], "\xaa\xbb", 2);
}

ZTEST(nfc_st25dv, test_zero_length_read_and_write_do_not_touch_the_bus) {
  uint8_t byte = 0;

  zassert_ok(eeprom_st25dv_read(dev, 0, &byte, 0));
  zassert_ok(eeprom_st25dv_write(dev, 0, &byte, 0));
  zassert_ok(eeprom_st25dv_read_sys(dev, 0, &byte, 0));
  zassert_ok(eeprom_st25dv_write_sys(dev, 0, &byte, 0));
  zassert_equal(st25dv_emul_xfers(), 0);
}

ZTEST(nfc_st25dv, test_dynamic_registers_are_read_at_0x53_from_2000h) {
  uint8_t eh = 0;

  zassert_ok(st25dv_read_eh_ctrl_dyn(dev, &eh));
  expect_wr(last_xfer(), 0x53, "20 02");
  zassert_equal(eh, ST25DV_EMUL_VCC_ON, "VCC on (the lens powers the tag), EH off");
}

ZTEST(nfc_st25dv, test_system_area_is_read_at_0x57) {
  uint8_t ref = 0;

  zassert_ok(st25dv_read_id(dev, &ref));
  expect_wr(last_xfer(), 0x57, "00 17");
  zassert_equal(ref, 0x50, "IC_REF of the ST25DV04KC");
}

ZTEST(nfc_st25dv, test_system_area_is_written_at_0x57) {
  open_session();
  zassert_ok(st25dv_configure_gpo(dev, 0x91));
  expect_wr(last_xfer(), 0x57, "00 00 91");
  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO1), 0x91);
}

/* ---- retries ---- */

ZTEST(nfc_st25dv, test_read_from_a_silent_chip_fails_after_20_attempts) {
  uint8_t buf[2];
  int64_t start = g1_clock_uptime_ms();

  st25dv_emul_set_nak(true);

  zassert_equal(eeprom_st25dv_read(dev, 0x0010, buf, sizeof(buf)), -EIO);
  zassert_equal(st25dv_emul_xfers(), 20);
  expect_elapsed_ms(start, 39, 82);
}

ZTEST(nfc_st25dv, test_read_succeeds_when_the_chip_answers_on_the_20th_attempt) {
  uint8_t byte = 0;

  st25dv_emul_user_mem()[0x10] = 0x77;
  st25dv_emul_fail_next(19);

  zassert_ok(eeprom_st25dv_read(dev, 0x0010, &byte, 1));
  zassert_equal(byte, 0x77);
  zassert_equal(st25dv_emul_xfers(), 20);
}

ZTEST(nfc_st25dv, test_write_to_a_silent_chip_fails_after_20_attempts_and_changes_nothing) {
  st25dv_emul_set_nak(true);

  zassert_equal(eeprom_st25dv_write(dev, 0x0010, "\x01\x02", 2), -EIO);
  zassert_equal(st25dv_emul_xfers(), 20);
  zassert_equal(st25dv_emul_user_mem()[0x10], 0x00);
}

ZTEST(nfc_st25dv, test_system_read_and_write_fail_on_a_silent_chip) {
  uint8_t byte = 0;

  st25dv_emul_set_nak(true);

  zassert_equal(eeprom_st25dv_read_sys(dev, 0x0017, &byte, 1), -EIO);
  zassert_equal(eeprom_st25dv_write_sys(dev, 0x0000, &byte, 1), -EIO);
  zassert_equal(st25dv_emul_xfers(), 40);
}

ZTEST(nfc_st25dv, test_access_right_after_a_write_waits_out_the_programming_cycle) {
  uint8_t data[16];
  uint8_t back[16];

  g1_fill_pattern(data, sizeof(data), 0xa0);

  zassert_ok(eeprom_st25dv_write(dev, 0x0040, data, sizeof(data)));
  zassert_ok(eeprom_st25dv_read(dev, 0x0040, back, sizeof(back)));
  zassert_mem_equal(back, data, sizeof(data));
  zassert_true(st25dv_emul_xfers() > 2, "the read was retried during the write cycle");
}

ZTEST(nfc_st25dv, test_user_memory_write_fails_while_the_mailbox_is_enabled) {
  st25dv_emul_enable_mailbox();

  zassert_equal(eeprom_st25dv_write(dev, 0x0004, "\x03\x00", 2), -EIO);
  zassert_equal(st25dv_emul_user_mem()[4], 0x00);
}

/* ---- identity ---- */

ZTEST(nfc_st25dv, test_uid_is_read_as_8_bytes_lsb_first) {
  static const uint8_t uid[8] = {0x10, 0x32, 0x54, 0x76, 0x98, 0x51, 0x02, 0xe0};
  uint8_t got[8] = {0};

  st25dv_emul_set_uid(uid);

  zassert_ok(ST25DV_GetUID(dev, got));
  zassert_mem_equal(got, uid, 8);
  zassert_equal(got[7], 0xe0, "UID byte 7 (MSB) is E0h");
  zassert_equal(got[6], 0x02, "UID byte 6 is the IC manufacturer, ST");
  expect_wr(last_xfer(), 0x57, "00 18");
  zassert_equal(last_xfer()->rd_len, 8);
}

ZTEST(nfc_st25dv, test_ic_revision_is_read_from_0020h) {
  uint8_t rev = 0;

  st25dv_emul_set_ic_rev(0x13);
  zassert_ok(st25dv_read_ic_rev(dev, &rev));
  zassert_equal(rev, 0x13);
}

/* MEM_SIZE = blocks - 1, BLK_SIZE = bytes per block - 1; the 64KC's 07FFh shows the byte order */
ZTEST(nfc_st25dv, test_memory_size_is_read_lsb_first_as_blocks_and_block_bytes_minus_1) {
  static const struct {
    const char *chip;
    uint16_t mem_size;
    uint8_t blk_size;
  } rows[] = {
    {"04KC", 0x007f, 0x03}, /* 128 blocks of 4 bytes */
    {"64KC", 0x07ff, 0x03}, /* 2048 blocks of 4 bytes */
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    struct st25dv_mem_size size = {0};

    st25dv_emul_set_mem_size(rows[row].mem_size, rows[row].blk_size);
    zassert_ok(st25dv_read_mem_size(dev, &size), "%s", rows[row].chip);
    zassert_equal(size.mem_size, rows[row].mem_size, "%s: MEM_SIZE 0x%04x, expected 0x%04x", rows[row].chip,
                  size.mem_size, rows[row].mem_size);
    zassert_equal(size.blk_size, rows[row].blk_size, "%s: BLK_SIZE 0x%02x, expected 0x%02x", rows[row].chip,
                  size.blk_size, rows[row].blk_size);
  }
}

ZTEST(nfc_st25dv, test_identity_helpers_reject_null_and_report_bus_errors) {
  uint8_t byte;
  struct st25dv_mem_size size;

  zassert_equal(ST25DV_GetUID(dev, NULL), -EINVAL);
  zassert_equal(st25dv_read_ic_rev(dev, NULL), -EINVAL);
  zassert_equal(st25dv_read_id(dev, NULL), -EINVAL);
  zassert_equal(st25dv_read_mem_size(dev, NULL), -EINVAL);
  zassert_equal(st25dv_read_it_pulse(dev, NULL), -EINVAL);
  zassert_equal(st25dv_emul_xfers(), 0);

  st25dv_emul_set_nak(true);
  zassert_equal(st25dv_read_id(dev, &byte), -EIO);
  zassert_equal(st25dv_read_mem_size(dev, &size), -EIO);
}

/* ---- password ---- */

ZTEST(nfc_st25dv, test_password_is_sent_msb_first_with_code_09_and_repeated) {
  static const struct st25dv_passwd pwd = {.msb = 0x01234567, .lsb = 0x89abcdef};

  st25dv_present_i2c_password(dev, &pwd);
  zassert_equal(st25dv_emul_xfers(), 1);
  /* I2C_PWD (0900h), the password, validate (09), the password again */
  expect_wr(last_xfer(), 0x57, "09 00 01 23 45 67 89 ab cd ef 09 01 23 45 67 89 ab cd ef");
}

ZTEST(nfc_st25dv, test_right_password_opens_the_session_and_a_wrong_one_closes_it) {
  static const uint8_t stored[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  static const struct st25dv_passwd right = {.msb = 0x01234567, .lsb = 0x89abcdef};
  static const struct st25dv_passwd wrong = {.msb = 0x01234567, .lsb = 0x89abcdee};
  uint8_t session = 0xff;

  st25dv_emul_set_password(stored);

  zassert_ok(st25dv_read_i2c_security_session_dyn(dev, &session));
  zassert_equal(session, 0, "closed after power-on");
  zassert_ok(st25dv_present_i2c_password(dev, &right));
  zassert_ok(st25dv_read_i2c_security_session_dyn(dev, &session));
  zassert_equal(session, 1);
  zassert_ok(st25dv_present_i2c_password(dev, &wrong));
  zassert_ok(st25dv_read_i2c_security_session_dyn(dev, &session));
  zassert_equal(session, 0);
}

ZTEST(nfc_st25dv, test_session_status_of_a_silent_chip_is_closed_and_an_error) {
  uint8_t session = 0xff;

  open_session();
  st25dv_emul_set_nak(true);

  zassert_equal(st25dv_read_i2c_security_session_dyn(dev, &session), -EIO);
  zassert_equal(session, 0);
}

/* ---- GPO ---- */

ZTEST(nfc_st25dv, test_gpo_configuration_is_refused_without_the_session) {
  zassert_not_equal(st25dv_configure_gpo(dev, 0xff), 0);
  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO1), 0x11, "factory value");

  open_session();
  zassert_ok(st25dv_configure_gpo(dev, 0xff));
  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO1), 0xff);
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_GPO_CTRL), 0x01, "GPO_EN copied to GPO_CTRL_Dyn");
}

ZTEST(nfc_st25dv, test_it_pulse_read_returns_gpo2) {
  uint8_t pulse = 0;

  zassert_ok(st25dv_read_it_pulse(dev, &pulse));
  zassert_equal(pulse, 0x0c, "GPO2 factory value: IT_TIME 011b");
}

ZTEST(nfc_st25dv, test_it_pulse_write_replaces_the_low_5_bits_of_gpo2) {
  st25dv_emul_set_sys(ST25DV_EMUL_SYS_GPO2, 0xe3);
  open_session();

  zassert_ok(st25dv_write_it_pulse(dev, 0xfc));
  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO2), 0xfc);
  g1_clock_advance_ms(10);
  zassert_ok(st25dv_write_it_pulse(dev, 0x01));
  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO2), 0xe1);
}

ZTEST(nfc_st25dv, test_interrupt_status_reports_the_field_and_clears_when_read) {
  uint8_t status = 0;

  st25dv_emul_rf_field(true);

  zassert_ok(st25dv_read_itsts_dyn(dev, &status));
  zassert_equal(status, ST25DV_EMUL_IT_FIELD_RISING);
  zassert_ok(st25dv_read_itsts_dyn(dev, &status));
  zassert_equal(status, 0);
}

ZTEST(nfc_st25dv, test_energy_harvesting_enable_is_written_to_eh_ctrl_dyn) {
  uint8_t eh = 0;

  zassert_ok(st25dv_write_eh_ctrl_dyn(dev, 0x01));
  zassert_ok(st25dv_read_eh_ctrl_dyn(dev, &eh));
  zassert_equal(eh, ST25DV_EMUL_VCC_ON | ST25DV_EMUL_EH_ON | ST25DV_EMUL_EH_EN);
}

/* ---- mailbox ---- */

ZTEST(nfc_st25dv, test_mailbox_read_returns_the_case_message_and_its_length_minus_1) {
  static const uint8_t msg[] = {0x13, 0x01, 0x02, 0x02, 0x32, 0x06, 0x50};
  uint8_t buf[256];
  uint8_t *out = buf;
  uint32_t len = 0;
  int first;

  st25dv_emul_enable_mailbox();
  zassert_ok(st25dv_emul_rf_put_msg(msg, sizeof(msg)));
  first = st25dv_emul_xfers();

  zassert_ok(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &out, &len));
  zassert_equal(len, sizeof(msg) - 1, "MB_LEN_Dyn: length - 1");
  zassert_mem_equal(buf, msg, sizeof(msg));
  zassert_equal(st25dv_emul_xfers() - first, 3);
  /* MB_CTRL_Dyn, MB_LEN_Dyn, the mailbox */
  expect_wr(st25dv_emul_xfer(first), 0x53, "20 06");
  expect_wr(st25dv_emul_xfer(first + 1), 0x53, "20 07");
  expect_wr(st25dv_emul_xfer(first + 2), 0x53, "20 08");
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_CTRL) & ST25DV_EMUL_RF_PUT_MSG, 0,
                "the whole message was read: the mailbox is free again");
}

ZTEST(nfc_st25dv, test_mailbox_read_without_a_case_message_returns_minus_3) {
  uint8_t buf[256];
  uint8_t *out = buf;
  uint32_t len = 0;

  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &out, &len), -3, "FTM off");
  st25dv_emul_enable_mailbox();
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &out, &len), -3, "empty");
}

ZTEST(nfc_st25dv, test_mailbox_read_from_a_silent_chip_returns_minus_2) {
  uint8_t buf[256];
  uint8_t *out = buf;
  uint32_t len = 0;

  st25dv_emul_set_nak(true);
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &out, &len), -2);
}

ZTEST(nfc_st25dv, test_mailbox_helpers_reject_null_arguments) {
  uint8_t buf[4];
  uint8_t *out = buf;
  uint32_t len;

  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(NULL, &out, &len), -1);
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, NULL, &len), -1);
  zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &out, NULL), -1);
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(NULL, buf, 1), -1);
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, NULL, 1), -1);
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, buf, 0), -1);
  zassert_equal(st25dv_emul_xfers(), 0);
}

ZTEST(nfc_st25dv, test_mailbox_write_leaves_the_reply_for_the_case) {
  static const uint8_t reply[] = {0x24, 0x01, 0x03, 0x03, 0x01, 0x06, 0x06, 0x38};
  uint8_t got[256];

  st25dv_emul_enable_mailbox();

  zassert_ok(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, reply, sizeof(reply)));
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_LEN), sizeof(reply) - 1);
  zassert_equal(st25dv_emul_rf_get_msg(got, sizeof(got)), sizeof(reply));
  zassert_mem_equal(got, reply, sizeof(reply));
}

ZTEST(nfc_st25dv, test_mailbox_write_over_an_unread_case_message_is_refused) {
  st25dv_emul_enable_mailbox();
  zassert_ok(st25dv_emul_rf_put_msg("\x13\x01\x02\x00", 4));

  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x24\x01", 2), -3);
  zassert_equal(st25dv_emul_mailbox()[0], 0x13, "the case's message is intact");
}

ZTEST(nfc_st25dv, test_mailbox_write_over_an_unread_reply_is_refused) {
  st25dv_emul_enable_mailbox();
  zassert_ok(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x24\x01", 2));

  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x13\x01", 2), -3);
  zassert_equal(st25dv_emul_mailbox()[0], 0x24);
}

ZTEST(nfc_st25dv, test_mailbox_write_with_fast_transfer_mode_off_fails) {
  zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x24\x01", 2), -3);
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_CTRL), 0);
}

/* MB_LEN is the length - 1; reading the last byte releases the mailbox */
ZTEST(nfc_st25dv, test_a_256_byte_case_message_is_read_whole) {
  uint8_t msg[256];
  uint8_t buf[256];
  uint8_t *out = buf;
  uint32_t len = 0;

  g1_fill_pattern(msg, sizeof(msg), 0);
  memset(buf, 0xee, sizeof(buf));
  st25dv_emul_enable_mailbox();
  zassert_ok(st25dv_emul_rf_put_msg(msg, sizeof(msg)));

  zassert_ok(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &out, &len));
  zassert_equal(len, 255);
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_CTRL) & ST25DV_EMUL_RF_PUT_MSG, 0, "mailbox still busy");
  zassert_mem_equal(buf, msg, sizeof(msg));
}

/* ---- lock ---- */

static void hold_lock_50_ms(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  eeprom_st25dv_lock(dev);
  k_sleep(K_MSEC(50));
  eeprom_st25dv_unlock(dev);
}

ZTEST(nfc_st25dv, test_tag_access_waits_for_the_lock_holder) {
  uint8_t byte;
  int64_t start;

  g1_thread_start(hold_lock_50_ms, NULL, K_PRIO_PREEMPT(1));
  k_sleep(K_MSEC(1));
  start = g1_clock_uptime_ms();

  zassert_ok(eeprom_st25dv_read(dev, 0, &byte, 1));
  expect_elapsed_ms(start, 48, 56);
}

ZTEST(nfc_st25dv, test_lock_and_unlock_ignore_a_missing_device) {
  eeprom_st25dv_lock(NULL);
  eeprom_st25dv_unlock(NULL);
}
