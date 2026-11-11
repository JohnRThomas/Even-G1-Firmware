/* Image values computed from aw9320x_fw_info.h's bytes by a separate script, not the driver. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "aw9320x_emul.h"

#include "aw9320x/aw9320x.c"
#include "aw9320x/aw93203_port.c"

#define E G1_AW9320X_EMUL

#define IMG_VERSION    0x06000007u
#define IMG_LEN        7068
#define IMG_DATA       (&fw_para_data[72])
#define IMG_CHECK_CODE 0x29c82e7du
#define OTHER_VERSION  0x06000006u

/* IRQEN 040Fh: init, proximity, exit, touch (bits 0..3), button (bit 10) */
#define TABLE_IRQEN    0x40fu

/* start + last-sector erase + check words, 63 erases, 7068 = 27 x 256 + 156 */
#define UPDATE_ERASES  63
#define UPDATE_XFERS   28
#define UPDATE_CMDS    (3 + UPDATE_ERASES + UPDATE_XFERS)

static int64_t t0;

static void touch_before(void *fixture)
{
	ARG_UNUSED(fixture);
	aw9320x_Global_var_rst_def_val();
	g_aw9320x_func = &aw9320x_func;
	aw9320x_wearing = UNKNOWN;
	aw93203_ready = 0;
	pt_comm_busy = 0;
}

ZTEST_SUITE(touch, NULL, NULL, touch_before, NULL, NULL);

static void mark(void)
{
	t0 = k_uptime_get();
}

static int64_t elapsed(void)
{
	return k_uptime_get() - t0;
}

static void init_ok(void)
{
	zassert_equal(aw9320x_init(&aw9320x_func), 0, "aw9320x_init failed");
}

#define expect_write(i, a, v)                                                                      \
	do {                                                                                       \
		int _i = (i);                                                                      \
		zassert_true(_i < aw9320x_emul_writes(E), "write %d missing (%d writes)", _i,      \
			     aw9320x_emul_writes(E));                                              \
		zassert_equal(aw9320x_emul_write_addr(E, _i), (a),                                 \
			      "write %d: register %04x, expected %04x", _i,                        \
			      aw9320x_emul_write_addr(E, _i), (a));                                \
		zassert_equal(aw9320x_emul_write_val(E, _i), (v),                                  \
			      "write %d to %04x: %08x, expected %08x", _i, (a),                    \
			      aw9320x_emul_write_val(E, _i), (v));                                 \
	} while (0)

#define expect_cmd(i, mod, cmd, sq, ad, len)                                                       \
	do {                                                                                       \
		int _i = (i);                                                                      \
		struct aw9320x_emul_cmd _c = aw9320x_emul_cmd(E, _i);                              \
		zassert_equal(_c.module, (mod), "command %d: module %u", _i, _c.module);           \
		zassert_equal(_c.command, (cmd), "command %d: command %u", _i, _c.command);        \
		zassert_equal(_c.seq, (sq), "command %d: seq %u", _i, _c.seq);                     \
		zassert_equal(_c.addr, (ad), "command %d: address %08x", _i, _c.addr);             \
		zassert_equal(_c.length, (len), "command %d: length %u", _i, _c.length);           \
		zassert_equal(_c.status, 0, "command %d: status %u", _i, _c.status);               \
	} while (0)

static int expect_table(int w)
{
	for (size_t i = 0; i < ARRAY_SIZE(aw9320x_reg_default); i += 2) {
		if (aw9320x_reg_default[i] == 0x4414) {
			continue;
		}
		expect_write(w, aw9320x_reg_default[i], aw9320x_reg_default[i + 1]);
		w++;
	}
	return w;
}

static void expect_update_cleanup(void)
{
	int n = aw9320x_emul_writes(E);

	expect_write(n - 4, 0xff20, 0x3c00f091);
	expect_write(n - 3, 0x4748, 0);
	expect_write(n - 2, 0x4744, 0);
	expect_write(n - 1, 0xff20, 0x3c00f011);
}

static void expect_image_installed(void)
{
	const uint8_t *flash = aw9320x_emul_flash(E);

	zassert_mem_equal(&flash[0x2000], IMG_DATA, IMG_LEN, "image in flash differs");
	for (uint32_t a = 0x2000 + IMG_LEN; a < 0x3f80; a++) {
		zassert_equal(flash[a], 0xff, "byte %04x after the image not erased", a);
	}
	zassert_equal(aw9320x_emul_flash_word(E, 0x3ff8), AW9320X_EMUL_CHECK_EN);
	zassert_equal(aw9320x_emul_flash_word(E, 0x3ffc), IMG_CHECK_CODE, "check code %08x",
		      aw9320x_emul_flash_word(E, 0x3ffc));
}

/* ---- init ---- */

/* Soft reset 3Ch to FF18h, wrapped in access key (FF20h) and flash wake-up (4700h) writes. */
ZTEST(touch, test_init_resets_the_chip_and_loads_the_parameter_table)
{
	int w = 0;

	init_ok();

	expect_write(w++, 0xff20, 0x3c00f091);
	expect_write(w++, 0x4700, 0x110);
	expect_write(w++, 0x4444, 0);
	expect_write(w++, 0xff18, 0x3c);
	expect_write(w++, 0x4700, 0x102);
	w = expect_table(w);
	expect_write(w++, 0x4408, 1);
	expect_write(w++, 0x4414, TABLE_IRQEN);
	/* 5 reset writes, 43 table entries, mode, IRQEN */
	zassert_equal(w, 50);
	zassert_equal(aw9320x_emul_writes(E), w);
	zassert_equal(aw9320x_emul_soft_resets(E), 1);
	zassert_equal(aw9320x_emul_cmds(E), 0, "same firmware version: no update");
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_IRQEN), TABLE_IRQEN);
}

/* 1 + 20 + 25 ms of sleeps cover Tinit (20 ms) */
ZTEST(touch, test_init_waits_out_the_reset_without_polling)
{
	mark();
	init_ok();
	zassert_true(elapsed() >= 46 && elapsed() <= 48, "init took %lld ms", elapsed());
}

ZTEST(touch, test_init_accepts_the_three_supported_chip_ids)
{
	static const uint32_t ids[] = { 0x20223203, 0x20223205, 0x20223207 };

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		aw9320x_emul_set_chip_id(E, ids[i]);
		zassert_equal(aw9320x_init(&aw9320x_func), 0, "chip id %08x rejected", ids[i]);
	}
}

ZTEST(touch, test_init_rejects_an_unknown_chip_id_before_writing_anything)
{
	static const uint32_t ids[] = { 0x00000000, 0x20223204, 0x12345678 };

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		aw9320x_emul_set_chip_id(E, ids[i]);
		zassert_equal(aw9320x_init(&aw9320x_func), -AW_ERR, "chip id %08x accepted",
			      ids[i]);
	}
	zassert_equal(aw9320x_emul_writes(E), 0);
	zassert_equal(aw9320x_emul_soft_resets(E), 0);
}

ZTEST(touch, test_init_on_a_nak_reads_the_chip_id_three_times_then_fails)
{
	aw9320x_emul_set_nak(E, true);

	mark();
	zassert_equal(aw9320x_init(&aw9320x_func), -AW_ERR);
	zassert_equal(aw9320x_emul_transfers(E), 3);
	zassert_true(elapsed() >= 3 && elapsed() <= 4, "%lld ms", elapsed());
}

ZTEST(touch, test_init_retries_a_transient_nak)
{
	aw9320x_emul_nak_next(E, 2);

	init_ok();
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
}

ZTEST(touch, test_init_without_the_init_interrupt_reinstalls_the_firmware)
{
	aw9320x_emul_corrupt_fw(E);

	init_ok();
	zassert_equal(aw9320x_emul_count_writes(E, 0x4408, 0x14), 1, "online upgrade command");
	zassert_equal(aw9320x_emul_cmds(E), UPDATE_CMDS);
	expect_image_installed();
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
}

ZTEST(touch, test_port_init_sets_ready_only_on_success)
{
	aw9320x_emul_set_chip_id(E, 0x12345678);
	aw93203_init();
	zassert_equal(aw93203_ready, 0);

	aw9320x_emul_set_chip_id(E, AW9320X_EMUL_CHIP_ID);
	aw93203_init();
	zassert_equal(aw93203_ready, 1);
}

/* ---- check sum ---- */

ZTEST(touch, test_check_sum_accepts_the_shipped_image)
{
	zassert_equal(sizeof(fw_para_data), 7140);
	zassert_equal(aw9320x_check_sum(fw_para_data, sizeof(fw_para_data)), AW_OK);
}

ZTEST(touch, test_check_sum_rejects_a_changed_byte)
{
	static uint8_t img[sizeof(fw_para_data)];

	memcpy(img, fw_para_data, sizeof(img));
	img[72 + 1000] ^= 0x01;
	zassert_equal(aw9320x_check_sum(img, sizeof(img)), -AW_ERR);
}

ZTEST(touch, test_check_sum_is_a_byte_sum_stored_little_endian)
{
	static const uint8_t le[] = { 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0xfe };
	static const uint8_t be[] = { 0x00, 0x00, 0x01, 0x04, 0x01, 0x02, 0x03, 0xfe };

	/* 01h + 02h + 03h + FEh = 104h */
	zassert_equal(aw9320x_check_sum(le, sizeof(le)), AW_OK);
	zassert_equal(aw9320x_check_sum(be, sizeof(be)), -AW_ERR);
}

/* ---- mode ---- */

/* REG_CMD 2 sleep, 1 active */
ZTEST(touch, test_mode_set_sleep_and_active_write_cmd_and_get_reads_them_back)
{
	int w;

	init_ok();
	w = aw9320x_emul_writes(E);

	aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
	expect_write(w, 0x4408, 2);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_SLEEP_MODE);

	aw9320x_mode_operation_set(AW9320X_ACTIVE_MODE);
	expect_write(w + 1, 0x4408, 1);
	zassert_equal(aw9320x_emul_writes(E), w + 2);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_ACTIVE_MODE);
}

/* deep sleep needs the HOSTCTRL1 wake-up first */
ZTEST(touch, test_mode_set_leaving_deep_sleep_wakes_the_chip_first)
{
	int w;

	init_ok();
	w = aw9320x_emul_writes(E);

	aw9320x_mode_operation_set(AW9320X_DEEPSLEEP_MODE);
	expect_write(w, 0x4408, 3);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_DEEPSLEEP_MODE);

	aw9320x_mode_operation_set(AW9320X_ACTIVE_MODE);
	expect_write(w + 1, 0xff10, 1);
	expect_write(w + 2, 0x4408, 1);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_ACTIVE_MODE);

	aw9320x_mode_operation_set(AW9320X_DEEPSLEEP_MODE);
	aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
	expect_write(w + 4, 0xff10, 1);
	expect_write(w + 5, 0x4408, 2);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_SLEEP_MODE);
}

ZTEST(touch, test_mode_set_to_the_current_mode_writes_nothing)
{
	int w;

	init_ok();
	w = aw9320x_emul_writes(E);
	aw9320x_mode_operation_set(AW9320X_ACTIVE_MODE);
	zassert_equal(aw9320x_emul_writes(E), w);
}

ZTEST(touch, test_mode_set_waits_one_scan_period)
{
	init_ok();
	mark();
	aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
	zassert_true(elapsed() >= 50 && elapsed() <= 51, "%lld ms", elapsed());
}

ZTEST(touch, test_mode_get_unknown_work_state_is_an_error)
{
	init_ok();
	aw9320x_emul_set_reg(E, AW9320X_EMUL_REG_WST, 0x02000000);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_MODE_ERR);
}

/* DEFECT: the WST read result is ignored; a silent chip reads as deep sleep. */
ZTEST_EXPECT_FAIL(touch, test_mode_get_on_a_nak_is_an_error);
ZTEST(touch, test_mode_get_on_a_nak_is_an_error)
{
	init_ok();
	aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
	aw9320x_emul_set_nak(E, true);
	zassert_equal(aw9320x_mode_operation_get(), AW9320X_MODE_ERR);
}

/* ---- packets ---- */

ZTEST(touch, test_pack_checksum_is_minus_the_sum_of_header_fields_and_words)
{
	uint8_t erase[4] = { 0x10, 0x00, 0x3f, 0x80 };
	uint8_t check[16] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x3f, 0xf8,
			      0x20, 0x22, 0x20, 0x22, 0x29, 0xc8, 0x2e, 0x7d };

	/* -(1 + 1 + 0) */
	zassert_equal(get_pack_checksum(NULL, 0, 1, 1), 0xfffffffe);
	/* -(2 + 2 + 4 + 10003F80h) */
	zassert_equal(get_pack_checksum(erase, 4, 2, 2), 0xefffc078);
	/* -(1 + 2 + 16 + 0 + 10003FF8h + 20222022h + 29C82E7Dh) */
	zassert_equal(get_pack_checksum(check, 16, 1, 2), 0xa6157156);
}

/* header 0002h, size, checksum, module, command, length, value; MSB first */
ZTEST(touch, test_pack_send_writes_the_packet_at_0800_msb_first)
{
	uint8_t addr[4] = { 0x10, 0x00, 0x3f, 0x80 };
	static const uint8_t want[16] = { 0x00, 0x02, 0x00, 0x10, 0xef, 0xff, 0xc0, 0x78,
					  0x02, 0x02, 0x00, 0x04, 0x10, 0x00, 0x3f, 0x80 };
	uint8_t got[16];

	zassert_equal(dri_to_soc_pack_send(2, 2, sizeof(addr), addr), AW_OK);
	zassert_equal(aw9320x_emul_transfers(E), 1);
	aw9320x_emul_read_sram(E, 0x0800, got, sizeof(got));
	zassert_mem_equal(got, want, sizeof(want));
}

ZTEST(touch, test_pack_send_tries_six_times_on_nak)
{
	aw9320x_emul_set_nak(E, true);
	zassert_equal(dri_to_soc_pack_send(1, 1, 0, NULL), -AW_ERR);
	zassert_equal(aw9320x_emul_transfers(E), 6);
}

static void put_ack(uint8_t module, uint8_t command, uint32_t status)
{
	uint8_t ack[20] = { 0x00, 0x02, 0x00, 0x14, 0, 0, 0, 0, module, command, 0x00, 0x08,
			    status >> 24, status >> 16, status >> 8, status };

	aw9320x_emul_write_sram(E, 0x1800, ack, sizeof(ack));
}

ZTEST(touch, test_pack_parse_accepts_the_ack_of_the_same_command)
{
	put_ack(1, 1, 0);
	zassert_equal(soc_to_dri_pack_parse(8, 1, 1), AW_OK);
}

ZTEST(touch, test_pack_parse_rejects_another_module_or_command)
{
	put_ack(1, 1, 0);
	zassert_equal(soc_to_dri_pack_parse(8, 2, 1), -AW_ERR);
	zassert_equal(soc_to_dri_pack_parse(8, 1, 2), -AW_ERR);
}

ZTEST(touch, test_pack_parse_rejects_a_non_zero_status)
{
	put_ack(1, 1, 1);
	zassert_equal(soc_to_dri_pack_parse(8, 1, 1), -AW_ERR);
	put_ack(1, 1, 0x01000000);
	zassert_equal(soc_to_dri_pack_parse(8, 1, 1), -AW_ERR);
}

static int heap_blocks(void)
{
	static void *blk[4096];
	int n = 0;

	while (n < (int)ARRAY_SIZE(blk) && (blk[n] = malloc(64)) != NULL) {
		n++;
	}
	zassert_true(n < (int)ARRAY_SIZE(blk), "heap larger than the probe");
	for (int i = 0; i < n; i++) {
		free(blk[i]);
	}
	return n;
}

/* DEFECT: the answer buffer leaks on a mismatch. */
ZTEST_EXPECT_FAIL(touch, test_pack_parse_failure_frees_its_buffer);
ZTEST(touch, test_pack_parse_failure_frees_its_buffer)
{
	int before;

	put_ack(1, 1, 0);
	before = heap_blocks();
	for (int i = 0; i < 64; i++) {
		zassert_equal(soc_to_dri_pack_parse(8, 2, 2), -AW_ERR);
	}
	zassert_equal(heap_blocks(), before, "heap lost %d blocks of 64 bytes",
		      before - heap_blocks());
}

ZTEST(touch, test_pack_round_trip_through_the_boot_loader)
{
	uint8_t xfer[12] = { 0, 0, 0, 1, 0x10, 0x00, 0x20, 0x00, 0xaa, 0xbb, 0xcc, 0xdd };
	struct aw9320x_emul_cmd c;

	zassert_equal(aw9320x_i2c_write(0x4408, 0x14), AW_OK);
	zassert_equal(dri_to_soc_pack_send(1, 2, sizeof(xfer), xfer), AW_OK);
	zassert_equal(aw9320x_i2c_write(0x1c10, 0x20000800), AW_OK);
	zassert_equal(aw9320x_i2c_write(0x4408, 0x15), AW_OK);
	zassert_equal(soc_to_dri_pack_parse(8, 1, 2), AW_OK);

	zassert_equal(aw9320x_emul_cmds(E), 1);
	c = aw9320x_emul_cmd(E, 0);
	zassert_equal(c.status, 0, "packet rejected: status %u", c.status);
	zassert_equal(c.seq, 1);
	zassert_equal(c.addr, 0x10002000);
	zassert_equal(aw9320x_emul_flash_word(E, 0x2000), 0xaabbccdd);
}

/* ---- version compare ---- */

ZTEST(touch, test_fw_version_cmp_is_equal_only_for_the_same_version)
{
	int8_t cmp = -1;

	zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), AW_OK);
	zassert_equal(cmp, AW9320X_VER_EQUAL);

	aw9320x_emul_set_fw_version(E, IMG_VERSION - 1);
	zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), AW_OK);
	zassert_equal(cmp, AW9320X_VER_NOT_EQUAL);

	aw9320x_emul_set_fw_version(E, IMG_VERSION + 1);
	cmp = -1;
	zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), AW_OK);
	zassert_equal(cmp, AW9320X_VER_NOT_EQUAL);
}

ZTEST(touch, test_fw_version_cmp_on_a_nak_fails_and_keeps_the_result)
{
	int8_t cmp = 7;

	aw9320x_emul_set_nak(E, true);
	zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), -AW_ERR);
	zassert_equal(cmp, 7);
}

/* ---- fw update ---- */

ZTEST(touch, test_fw_update_with_the_same_version_changes_nothing)
{
	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
	zassert_equal(aw9320x_emul_writes(E), 0);
	zassert_equal(aw9320x_emul_cmds(E), 0);
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
}

ZTEST(touch, test_fw_update_sends_start_erase_check_words_erase_and_transfers_in_order)
{
	int i = 0;

	aw9320x_emul_set_fw_version(E, OTHER_VERSION);

	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
	expect_write(0, 0x4408, 0x14);
	zassert_equal(aw9320x_emul_cmds(E), UPDATE_CMDS);
	expect_cmd(i++, 1, 1, 0, 0, 0);
	expect_cmd(i++, 2, 2, 0, 0x10003f80, 4);
	expect_cmd(i++, 1, 2, 0, 0x10003ff8, 16);
	for (uint32_t s = 0; s < UPDATE_ERASES; s++) {
		expect_cmd(i++, 2, 2, 0, 0x10002000 + s * 128, 4);
	}
	for (uint32_t seq = 1; seq <= UPDATE_XFERS; seq++) {
		expect_cmd(i++, 1, 2, seq, 0x10002000 + (seq - 1) * 256,
			   seq < UPDATE_XFERS ? 8 + 256 : 8 + 156);
	}
}

ZTEST(touch, test_fw_update_installs_the_image_and_check_words)
{
	aw9320x_emul_set_fw_version(E, OTHER_VERSION);

	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
	expect_image_installed();
	zassert_equal(aw9320x_emul_isp_ops(E, AW9320X_EMUL_ISP_READ), 3);
}

ZTEST(touch, test_fw_update_stops_the_boot_loader_and_waits_for_init)
{
	int i;

	aw9320x_emul_set_fw_version(E, OTHER_VERSION);

	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
	i = aw9320x_emul_find_write(E, 0x4444, 0);
	zassert_true(i > 0, "no MCFG write");
	expect_write(i, 0x4444, 0x10000);
	expect_write(i + 1, 0xff20, 0x3c00ffff);
	expect_write(i + 2, 0x4744, 0);
	expect_write(i + 3, 0xff20, 0x3c00f011);
	expect_write(i + 4, 0x4444, 0);
	zassert_equal(aw9320x_emul_writes(E), i + 5);
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
	zassert_false(aw9320x_emul_int_asserted(E), "init interrupt not read");
}

ZTEST(touch, test_fw_update_without_an_acknowledge_fails_and_cleans_up)
{
	aw9320x_emul_set_fw_version(E, OTHER_VERSION);
	aw9320x_emul_set_ack_irq(E, false);

	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), -AW_PROT_UPDATE_ERR);
	zassert_equal(aw9320x_emul_cmds(E), 0);
	expect_update_cleanup();
}

ZTEST(touch, test_fw_update_nak_in_the_middle_fails_and_cleans_up)
{
	aw9320x_emul_set_fw_version(E, OTHER_VERSION);
	aw9320x_emul_nak_after_cmds(E, 70, 3);

	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), -AW_PROT_UPDATE_ERR);
	zassert_equal(aw9320x_emul_cmds(E), 70);
	expect_update_cleanup();
	zassert_mem_equal(&aw9320x_emul_flash(E)[0x2000], IMG_DATA, 4 * 256);
	zassert_equal(aw9320x_emul_flash_word(E, 0x2000 + 4 * 256), 0xffffffff);
}

ZTEST(touch, test_fw_update_rejected_command_fails_and_cleans_up)
{
	aw9320x_emul_set_fw_version(E, OTHER_VERSION);
	aw9320x_emul_fail_cmd(E, 2);

	zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), -AW_PROT_UPDATE_ERR);
	zassert_equal(aw9320x_emul_cmds(E), 3);
	expect_update_cleanup();
}

ZTEST(touch, test_fw_update_direct_mode_ignores_the_version)
{
	zassert_equal(aw9320x_fw_update(AW_DIRECT_LOAD), AW_OK);
	zassert_equal(aw9320x_emul_cmds(E), UPDATE_CMDS);
	expect_image_installed();
}

ZTEST(touch, test_fw_update_direct_mode_failure_is_a_plain_error)
{
	aw9320x_emul_fail_cmd(E, 0);
	zassert_equal(aw9320x_fw_update(AW_DIRECT_LOAD), -AW_ERR);
}

/* 112 sectors: 37F8h in 128-byte sectors */
ZTEST(touch, test_update_err_handle_installs_through_isp_and_restarts_the_chip)
{
	const uint8_t *flash = aw9320x_emul_flash(E);

	zassert_equal(aw9320x_update_err_handle(), AW_OK);

	expect_write(0, 0xff20, 0x3c00ffff);
	expect_write(1, 0x4444, 0x10000);
	expect_write(2, 0x4820, 6);
	expect_write(3, 0x4794, 0x5a637955);
	zassert_equal(aw9320x_emul_isp_ops(E, AW9320X_EMUL_ISP_ERASE), 112);
	zassert_equal(aw9320x_emul_isp_ops(E, AW9320X_EMUL_ISP_PROGRAM), IMG_LEN / 4 + 2);
	zassert_equal(aw9320x_emul_isp_ops(E, AW9320X_EMUL_ISP_READ), IMG_LEN / 4);
	expect_image_installed();
	for (uint32_t a = 0x0800; a < 0x2000; a++) {
		zassert_equal(flash[a], 0xff, "byte %04x not erased", a);
	}
	zassert_equal(aw9320x_emul_soft_resets(E), 1);
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
}

ZTEST(touch, test_init_recovers_from_a_failed_update_through_isp)
{
	aw9320x_emul_set_fw_version(E, OTHER_VERSION);
	aw9320x_emul_nak_after_cmds(E, 70, 3);

	init_ok();
	zassert_equal(aw9320x_emul_isp_ops(E, AW9320X_EMUL_ISP_PROGRAM), IMG_LEN / 4 + 2);
	expect_image_installed();
	zassert_equal(aw9320x_emul_state(E), AW9320X_EMUL_APP);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_IRQEN), TABLE_IRQEN);
}

/* ---- irq stat ---- */

ZTEST(touch, test_irq_stat_proximity_is_in_ear)
{
	init_ok();
	zassert_true(aw9320x_emul_touch(E, true));
	zassert_true(aw9320x_emul_int_asserted(E));

	aw9320x_get_irq_stat();
	zassert_equal(aw9320x_wearing, IN_EAR);
	zassert_equal(aw9320x_emul_get_reg(E, AW9320X_EMUL_REG_IRQSRC), 0);
	zassert_false(aw9320x_emul_int_asserted(E));
}

ZTEST(touch, test_irq_stat_proximity_exit_is_out_of_ear)
{
	init_ok();
	aw9320x_emul_touch(E, true);
	aw9320x_get_irq_stat();
	zassert_true(aw9320x_emul_touch(E, false));

	aw9320x_get_irq_stat();
	zassert_equal(aw9320x_wearing, OUT_EAR);
	zassert_false(aw9320x_emul_int_asserted(E));
}

ZTEST(touch, test_irq_stat_init_interrupt_reloads_the_parameters)
{
	int w;

	init_ok();
	zassert_equal(aw9320x_i2c_write(0xff18, 0x3c), AW_OK);
	g1_clock_advance_ms(AW9320X_EMUL_TINIT_MS);
	zassert_true(aw9320x_emul_int_asserted(E));
	zassert_false(aw9320x_emul_touch(E, true), "touch reported without configuration");
	aw9320x_emul_touch(E, false);
	w = aw9320x_emul_writes(E);

	aw9320x_get_irq_stat();
	w = expect_table(w);
	expect_write(w++, 0x4408, 1);
	expect_write(w++, 0x4414, TABLE_IRQEN);
	zassert_equal(aw9320x_emul_writes(E), w);
	zassert_true(aw9320x_emul_touch(E, true));
}

ZTEST(touch, test_irq_stat_on_a_nak_changes_nothing)
{
	init_ok();
	aw9320x_emul_touch(E, true);
	aw9320x_emul_set_nak(E, true);

	aw9320x_get_irq_stat();
	zassert_equal(aw9320x_wearing, UNKNOWN);
	aw9320x_emul_set_nak(E, false);
	zassert_true(aw9320x_emul_int_asserted(E), "source lost");
}

ZTEST(touch, test_diff_get_drops_the_10_fraction_bits)
{
	int32_t diff = 1;

	init_ok();
	aw9320x_emul_set_touch_diff(E, -25000 * 1024);
	aw9320x_emul_touch(E, true);
	aw9320x_diff_get(&diff);
	zassert_equal(diff, -25000);

	aw9320x_emul_touch(E, false);
	aw9320x_diff_get(&diff);
	zassert_equal(diff, 0);
}
