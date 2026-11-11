#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "devices.h"
#include "nfc_test.h"

#include "nfc/st25dv.h"

#define E    TAG_E
#define I2C1 DEVICE_DT_GET(DT_NODELABEL(i2c1))

static const struct device *dev;

static void st25dv_before(void *fixture)
{
	ARG_UNUSED(fixture);
	nfc_wlc_test_reset();
	nfc_pt_test_reset();
	dev = eeprom_st25dv_init();
	zassert_not_null(dev);
}

ZTEST_SUITE(nfc_st25dv, NULL, NULL, st25dv_before, NULL, NULL);

static void open_session(void)
{
	static const struct st25dv_passwd zero;

	zassert_ok(st25dv_present_i2c_password(dev, &zero));
	zassert_true(st25dv_emul_session_open(E));
}

static const struct st25dv_emul_xfer *last_xfer(void)
{
	zassert_true(st25dv_emul_xfers(E) > 0, "no transfer");
	return st25dv_emul_xfer(E, st25dv_emul_xfers(E) - 1);
}

static void expect_wr(const struct st25dv_emul_xfer *x, uint8_t addr, const uint8_t *wr,
		      size_t n)
{
	zassert_equal(x->addr, addr, "device address 0x%02x", x->addr);
	zassert_equal(x->wr_len, n, "%u bytes written", x->wr_len);
	zassert_mem_equal(x->wr, wr, n);
}

/* ---- device ---- */

ZTEST(nfc_st25dv, test_init_returns_the_tag_device_on_i2c1)
{
	const struct eeprom_st25dv_config *cfg = dev->config;

	zassert_equal_ptr(dev, &global_st25dv_context);
	zassert_equal_ptr(cfg->bus, I2C1);
	zassert_equal(cfg->addr, 0x53);
	zassert_not_null(dev->api);
	zassert_not_null(dev->data);
}

ZTEST(nfc_st25dv, test_init_with_the_bus_not_ready_returns_null)
{
	g1_device_set_ready(I2C1, false);

	zassert_is_null(eeprom_st25dv_init());
}

/* DEFECT: size is hard-coded 0x1000; the 04KC has 512 bytes of user memory. */
ZTEST_EXPECT_FAIL(nfc_st25dv, test_eeprom_size_is_the_user_memory_of_the_tag);
ZTEST(nfc_st25dv, test_eeprom_size_is_the_user_memory_of_the_tag)
{
	zassert_equal(eeprom_get_size(dev), 512, "size %zu", eeprom_get_size(dev));
}

/* ---- addressing ---- */

ZTEST(nfc_st25dv, test_user_read_sends_the_16_bit_address_msb_first_to_0x53)
{
	static const uint8_t addr[] = {0x01, 0xa5};
	uint8_t buf[3];

	memcpy(&st25dv_emul_user_mem(E)[0x1a5], "\x11\x22\x33", 3);

	zassert_ok(eeprom_st25dv_read(dev, 0x01a5, buf, sizeof(buf)));
	expect_wr(last_xfer(), 0x53, addr, sizeof(addr));
	zassert_equal(last_xfer()->rd_len, 3);
	zassert_mem_equal(buf, "\x11\x22\x33", 3);
}

ZTEST(nfc_st25dv, test_user_write_is_one_transfer_of_address_and_data)
{
	static const uint8_t exp[] = {0x01, 0x23, 0xaa, 0xbb};

	zassert_ok(eeprom_st25dv_write(dev, 0x0123, "\xaa\xbb", 2));
	zassert_equal(st25dv_emul_xfers(E), 1);
	expect_wr(last_xfer(), 0x53, exp, sizeof(exp));
	zassert_equal(last_xfer()->rd_len, 0);
	zassert_mem_equal(&st25dv_emul_user_mem(E)[0x123], "\xaa\xbb", 2);
}

ZTEST(nfc_st25dv, test_zero_length_read_and_write_do_not_touch_the_bus)
{
	uint8_t b = 0;

	zassert_ok(eeprom_st25dv_read(dev, 0, &b, 0));
	zassert_ok(eeprom_st25dv_write(dev, 0, &b, 0));
	zassert_ok(eeprom_st25dv_read_sys(dev, 0, &b, 0));
	zassert_ok(eeprom_st25dv_write_sys(dev, 0, &b, 0));
	zassert_equal(st25dv_emul_xfers(E), 0);
}

ZTEST(nfc_st25dv, test_dynamic_registers_are_read_at_0x53_from_2000h)
{
	static const uint8_t addr[] = {0x20, 0x02};
	uint8_t eh = 0;

	zassert_ok(st25dv_read_eh_ctrl_dyn(dev, &eh));
	expect_wr(last_xfer(), 0x53, addr, sizeof(addr));
	zassert_equal(eh, ST25DV_EMUL_VCC_ON, "VCC on (the lens powers the tag), EH off");
}

ZTEST(nfc_st25dv, test_system_area_is_read_at_0x57)
{
	static const uint8_t addr[] = {0x00, 0x17};
	uint8_t ref = 0;

	zassert_ok(st25dv_read_id(dev, &ref));
	expect_wr(last_xfer(), 0x57, addr, sizeof(addr));
	zassert_equal(ref, 0x50, "IC_REF of the ST25DV04KC");
}

ZTEST(nfc_st25dv, test_system_area_is_written_at_0x57)
{
	static const uint8_t exp[] = {0x00, 0x00, 0x91};

	open_session();
	zassert_ok(st25dv_configure_gpo(dev, 0x91));
	expect_wr(last_xfer(), 0x57, exp, sizeof(exp));
	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO1), 0x91);
}

/* ---- retries ---- */

ZTEST(nfc_st25dv, test_read_from_a_silent_chip_fails_after_20_attempts)
{
	uint8_t b[2] = {0x5a, 0x5a};
	int64_t t0 = g1_clock_uptime_ms();
	int64_t dt;

	st25dv_emul_set_nak(E, true);

	zassert_equal(eeprom_st25dv_read(dev, 0x0010, b, sizeof(b)), -EIO);
	dt = g1_clock_uptime_ms() - t0;
	zassert_equal(st25dv_emul_xfers(E), 20);
	zassert_true(dt >= 39 && dt <= 82, "%lld ms", dt);
}

ZTEST(nfc_st25dv, test_read_succeeds_when_the_chip_answers_on_the_20th_attempt)
{
	uint8_t b = 0;

	st25dv_emul_user_mem(E)[0x10] = 0x77;
	st25dv_emul_fail_next(E, 19);

	zassert_ok(eeprom_st25dv_read(dev, 0x0010, &b, 1));
	zassert_equal(b, 0x77);
	zassert_equal(st25dv_emul_xfers(E), 20);
}

ZTEST(nfc_st25dv, test_write_to_a_silent_chip_fails_after_20_attempts_and_changes_nothing)
{
	st25dv_emul_set_nak(E, true);

	zassert_equal(eeprom_st25dv_write(dev, 0x0010, "\x01\x02", 2), -EIO);
	zassert_equal(st25dv_emul_xfers(E), 20);
	zassert_equal(st25dv_emul_user_mem(E)[0x10], 0x00);
}

ZTEST(nfc_st25dv, test_system_read_and_write_fail_on_a_silent_chip)
{
	uint8_t b = 0;

	st25dv_emul_set_nak(E, true);

	zassert_equal(eeprom_st25dv_read_sys(dev, 0x0017, &b, 1), -EIO);
	zassert_equal(eeprom_st25dv_write_sys(dev, 0x0000, &b, 1), -EIO);
	zassert_equal(st25dv_emul_xfers(E), 40);
}

ZTEST(nfc_st25dv, test_access_right_after_a_write_waits_out_the_programming_cycle)
{
	uint8_t data[16];
	uint8_t back[16];

	for (int i = 0; i < 16; i++) {
		data[i] = (uint8_t)(0xa0 + i);
	}

	zassert_ok(eeprom_st25dv_write(dev, 0x0040, data, sizeof(data)));
	zassert_ok(eeprom_st25dv_read(dev, 0x0040, back, sizeof(back)));
	zassert_mem_equal(back, data, sizeof(data));
	zassert_true(st25dv_emul_xfers(E) > 2, "the read was retried during the write cycle");
}

ZTEST(nfc_st25dv, test_user_memory_write_fails_while_the_mailbox_is_enabled)
{
	st25dv_emul_enable_mailbox(E);

	zassert_equal(eeprom_st25dv_write(dev, 0x0004, "\x03\x00", 2), -EIO);
	zassert_equal(st25dv_emul_user_mem(E)[4], 0x00);
}

/* ---- identity ---- */

ZTEST(nfc_st25dv, test_uid_is_read_as_8_bytes_lsb_first)
{
	static const uint8_t uid[8] = {0x10, 0x32, 0x54, 0x76, 0x98, 0x51, 0x02, 0xe0};
	static const uint8_t addr[] = {0x00, 0x18};
	uint8_t got[8] = {0};

	st25dv_emul_set_uid(E, uid);

	zassert_ok(ST25DV_GetUID(dev, got));
	zassert_mem_equal(got, uid, 8);
	zassert_equal(got[7], 0xe0, "UID byte 7 (MSB) is E0h");
	zassert_equal(got[6], 0x02, "UID byte 6 is the IC manufacturer, ST");
	expect_wr(last_xfer(), 0x57, addr, sizeof(addr));
	zassert_equal(last_xfer()->rd_len, 8);
}

ZTEST(nfc_st25dv, test_ic_revision_is_read_from_0020h)
{
	uint8_t rev = 0;

	st25dv_emul_set_ic_rev(E, 0x13);
	zassert_ok(st25dv_read_ic_rev(dev, &rev));
	zassert_equal(rev, 0x13);
}

/* MEM_SIZE = blocks - 1, BLK_SIZE = bytes per block - 1; the 64KC's 07FFh shows the byte order */
ZTEST(nfc_st25dv, test_memory_size_is_read_lsb_first_as_blocks_and_block_bytes_minus_1)
{
	static const struct {
		const char *chip;
		uint16_t mem_size;
		uint8_t blk_size;
	} rows[] = {
		{"04KC", 0x007f, 0x03}, /* 128 blocks of 4 bytes */
		{"64KC", 0x07ff, 0x03}, /* 2048 blocks of 4 bytes */
	};

	for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
		struct st25dv_mem_size size = {0};

		st25dv_emul_set_mem_size(E, rows[i].mem_size, rows[i].blk_size);
		zassert_ok(st25dv_read_mem_size(dev, &size), "%s", rows[i].chip);
		zassert_equal(size.mem_size, rows[i].mem_size,
			      "%s: MEM_SIZE 0x%04x, expected 0x%04x", rows[i].chip, size.mem_size,
			      rows[i].mem_size);
		zassert_equal(size.blk_size, rows[i].blk_size,
			      "%s: BLK_SIZE 0x%02x, expected 0x%02x", rows[i].chip, size.blk_size,
			      rows[i].blk_size);
	}
}

ZTEST(nfc_st25dv, test_identity_helpers_reject_null_and_report_bus_errors)
{
	uint8_t b;
	struct st25dv_mem_size size;

	zassert_equal(ST25DV_GetUID(dev, NULL), -EINVAL);
	zassert_equal(st25dv_read_ic_rev(dev, NULL), -EINVAL);
	zassert_equal(st25dv_read_id(dev, NULL), -EINVAL);
	zassert_equal(st25dv_read_mem_size(dev, NULL), -EINVAL);
	zassert_equal(st25dv_read_it_pulse(dev, NULL), -EINVAL);
	zassert_equal(st25dv_emul_xfers(E), 0);

	st25dv_emul_set_nak(E, true);
	zassert_equal(st25dv_read_id(dev, &b), -EIO);
	zassert_equal(st25dv_read_mem_size(dev, &size), -EIO);
}

/* ---- password ---- */

ZTEST(nfc_st25dv, test_password_is_sent_msb_first_with_code_09_and_repeated)
{
	static const struct st25dv_passwd pwd = {.msb = 0x01234567, .lsb = 0x89abcdef};
	static const uint8_t exp[] = {
		0x09, 0x00,                                     /* I2C_PWD */
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0x09,                                           /* validate */
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	};

	st25dv_present_i2c_password(dev, &pwd);
	zassert_equal(st25dv_emul_xfers(E), 1);
	expect_wr(last_xfer(), 0x57, exp, sizeof(exp));
}

ZTEST(nfc_st25dv, test_right_password_opens_the_session_and_a_wrong_one_closes_it)
{
	static const uint8_t stored[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
	static const struct st25dv_passwd right = {.msb = 0x01234567, .lsb = 0x89abcdef};
	static const struct st25dv_passwd wrong = {.msb = 0x01234567, .lsb = 0x89abcdee};
	uint8_t session = 0xff;

	st25dv_emul_set_password(E, stored);

	zassert_ok(st25dv_read_i2c_security_session_dyn(dev, &session));
	zassert_equal(session, 0, "closed after power-on");
	zassert_ok(st25dv_present_i2c_password(dev, &right));
	zassert_ok(st25dv_read_i2c_security_session_dyn(dev, &session));
	zassert_equal(session, 1);
	zassert_ok(st25dv_present_i2c_password(dev, &wrong));
	zassert_ok(st25dv_read_i2c_security_session_dyn(dev, &session));
	zassert_equal(session, 0);
}

ZTEST(nfc_st25dv, test_session_status_of_a_silent_chip_is_closed_and_an_error)
{
	uint8_t session = 0xff;

	open_session();
	st25dv_emul_set_nak(E, true);

	zassert_equal(st25dv_read_i2c_security_session_dyn(dev, &session), -EIO);
	zassert_equal(session, 0);
}

/* ---- GPO ---- */

ZTEST(nfc_st25dv, test_gpo_configuration_is_refused_without_the_session)
{
	zassert_not_equal(st25dv_configure_gpo(dev, 0xff), 0);
	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO1), 0x11, "factory value");

	open_session();
	zassert_ok(st25dv_configure_gpo(dev, 0xff));
	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO1), 0xff);
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_GPO_CTRL), 0x01,
		      "GPO_EN copied to GPO_CTRL_Dyn");
}

ZTEST(nfc_st25dv, test_it_pulse_read_returns_gpo2)
{
	uint8_t it = 0;

	zassert_ok(st25dv_read_it_pulse(dev, &it));
	zassert_equal(it, 0x0c, "GPO2 factory value: IT_TIME 011b");
}

ZTEST(nfc_st25dv, test_it_pulse_write_replaces_the_low_5_bits_of_gpo2)
{
	st25dv_emul_set_sys(E, ST25DV_EMUL_SYS_GPO2, 0xe3);
	open_session();

	zassert_ok(st25dv_write_it_pulse(dev, 0xfc));
	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO2), 0xfc);
	g1_clock_advance_ms(10);
	zassert_ok(st25dv_write_it_pulse(dev, 0x01));
	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO2), 0xe1);
}

ZTEST(nfc_st25dv, test_interrupt_status_reports_the_field_and_clears_when_read)
{
	uint8_t it = 0;

	st25dv_emul_rf_field(E, true);

	zassert_ok(st25dv_read_itsts_dyn(dev, &it));
	zassert_equal(it, ST25DV_EMUL_IT_FIELD_RISING);
	zassert_ok(st25dv_read_itsts_dyn(dev, &it));
	zassert_equal(it, 0);
}

ZTEST(nfc_st25dv, test_energy_harvesting_enable_is_written_to_eh_ctrl_dyn)
{
	uint8_t eh = 0;

	zassert_ok(st25dv_write_eh_ctrl_dyn(dev, 0x01));
	zassert_ok(st25dv_read_eh_ctrl_dyn(dev, &eh));
	zassert_equal(eh, ST25DV_EMUL_VCC_ON | ST25DV_EMUL_EH_ON | ST25DV_EMUL_EH_EN);
}

/* ---- mailbox ---- */

ZTEST(nfc_st25dv, test_mailbox_read_returns_the_case_message_and_its_length_minus_1)
{
	static const uint8_t msg[] = {0x13, 0x01, 0x02, 0x02, 0x32, 0x06, 0x50};
	static const uint8_t ctrl[] = {0x20, 0x06};
	static const uint8_t len_addr[] = {0x20, 0x07};
	static const uint8_t mb_addr[] = {0x20, 0x08};
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len = 0;
	int n;

	st25dv_emul_enable_mailbox(E);
	zassert_ok(st25dv_emul_rf_put_msg(E, msg, sizeof(msg)));
	n = st25dv_emul_xfers(E);

	zassert_ok(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &p, &len));
	zassert_equal(len, sizeof(msg) - 1, "MB_LEN_Dyn: length - 1");
	zassert_mem_equal(buf, msg, sizeof(msg));
	zassert_equal(st25dv_emul_xfers(E) - n, 3);
	expect_wr(st25dv_emul_xfer(E, n), 0x53, ctrl, 2);
	expect_wr(st25dv_emul_xfer(E, n + 1), 0x53, len_addr, 2);
	expect_wr(st25dv_emul_xfer(E, n + 2), 0x53, mb_addr, 2);
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_MB_CTRL) & ST25DV_EMUL_RF_PUT_MSG, 0,
		      "the whole message was read: the mailbox is free again");
}

ZTEST(nfc_st25dv, test_mailbox_read_without_a_case_message_returns_minus_3)
{
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len = 0;

	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &p, &len), -3, "FTM off");
	st25dv_emul_enable_mailbox(E);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &p, &len), -3, "empty");
}

ZTEST(nfc_st25dv, test_mailbox_read_from_a_silent_chip_returns_minus_2)
{
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len = 0;

	st25dv_emul_set_nak(E, true);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &p, &len), -2);
}

ZTEST(nfc_st25dv, test_mailbox_helpers_reject_null_arguments)
{
	uint8_t buf[4];
	uint8_t *p = buf;
	uint32_t len;

	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(NULL, &p, &len), -1);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, NULL, &len), -1);
	zassert_equal(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &p, NULL), -1);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(NULL, buf, 1), -1);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, NULL, 1), -1);
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, buf, 0), -1);
	zassert_equal(st25dv_emul_xfers(E), 0);
}

ZTEST(nfc_st25dv, test_mailbox_write_leaves_the_reply_for_the_case)
{
	static const uint8_t reply[] = {0x24, 0x01, 0x03, 0x03, 0x01, 0x06, 0x06, 0x38};
	uint8_t got[256];

	st25dv_emul_enable_mailbox(E);

	zassert_ok(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, reply, sizeof(reply)));
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_MB_LEN), sizeof(reply) - 1);
	zassert_equal(st25dv_emul_rf_get_msg(E, got, sizeof(got)), sizeof(reply));
	zassert_mem_equal(got, reply, sizeof(reply));
}

ZTEST(nfc_st25dv, test_mailbox_write_over_an_unread_case_message_is_refused)
{
	st25dv_emul_enable_mailbox(E);
	zassert_ok(st25dv_emul_rf_put_msg(E, "\x13\x01\x02\x00", 4));

	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x24\x01", 2), -3);
	zassert_equal(st25dv_emul_mailbox(E)[0], 0x13, "the case's message is intact");
}

ZTEST(nfc_st25dv, test_mailbox_write_over_an_unread_reply_is_refused)
{
	st25dv_emul_enable_mailbox(E);
	zassert_ok(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x24\x01", 2));

	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x13\x01", 2), -3);
	zassert_equal(st25dv_emul_mailbox(E)[0], 0x24);
}

ZTEST(nfc_st25dv, test_mailbox_write_with_fast_transfer_mode_off_fails)
{
	zassert_equal(eeprom_st25dv_write_i2c_mb_mailbox_ram(dev, "\x24\x01", 2), -3);
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_MB_CTRL), 0);
}

/* DEFECT: always reads 255 bytes; a 256-byte message stays unread. */
ZTEST_EXPECT_FAIL(nfc_st25dv, test_a_256_byte_case_message_is_read_whole);
ZTEST(nfc_st25dv, test_a_256_byte_case_message_is_read_whole)
{
	uint8_t msg[256];
	uint8_t buf[256];
	uint8_t *p = buf;
	uint32_t len = 0;

	for (int i = 0; i < 256; i++) {
		msg[i] = (uint8_t)i;
	}
	memset(buf, 0xee, sizeof(buf));
	st25dv_emul_enable_mailbox(E);
	zassert_ok(st25dv_emul_rf_put_msg(E, msg, sizeof(msg)));

	zassert_ok(eeprom_st25dv_read_i2c_mb_mailbox_ram(dev, &p, &len));
	zassert_equal(len, 255);
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_MB_CTRL) & ST25DV_EMUL_RF_PUT_MSG, 0,
		      "mailbox still busy");
	zassert_mem_equal(buf, msg, sizeof(msg));
}

/* ---- lock ---- */

static K_THREAD_STACK_DEFINE(helper_stack, 2048);
static struct k_thread helper;
static int64_t helper_hold_ms;

static void hold_lock(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	eeprom_st25dv_lock(dev);
	k_sleep(K_MSEC(helper_hold_ms));
	eeprom_st25dv_unlock(dev);
}

static void run_helper(k_thread_entry_t fn)
{
	k_thread_create(&helper, helper_stack, K_THREAD_STACK_SIZEOF(helper_stack), fn, NULL,
			NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
}

ZTEST(nfc_st25dv, test_tag_access_waits_for_the_lock_holder)
{
	uint8_t b;
	int64_t t0;
	int64_t dt;

	helper_hold_ms = 50;
	run_helper(hold_lock);
	k_sleep(K_MSEC(1));
	t0 = g1_clock_uptime_ms();

	zassert_ok(eeprom_st25dv_read(dev, 0, &b, 1));
	dt = g1_clock_uptime_ms() - t0;
	k_thread_join(&helper, K_FOREVER);
	zassert_true(dt >= 48 && dt <= 56, "%lld ms", dt);
}

ZTEST(nfc_st25dv, test_lock_and_unlock_ignore_a_missing_device)
{
	eeprom_st25dv_lock(NULL);
	eeprom_st25dv_unlock(NULL);
}
