/* Image values computed from aw9320x_fw_info.h's bytes by a separate script, not the driver. */
#include "aw9320x_emul.h"

#include "aw9320x/aw93203_port.c"
#include "aw9320x/aw9320x.c"

#define IMG_VERSION    0x06000007u
#define IMG_LEN        7068
#define IMG_DATA       (&fw_para_data[72])
#define IMG_CHECK_CODE 0x29c82e7du
#define OTHER_VERSION  0x06000006u

/* IRQEN 040Fh: init, proximity, exit, touch (bits 0..3), button (bit 10) */
#define TABLE_IRQEN 0x40fu

/* start + last-sector erase + check words, 63 erases, 7068 = 27 x 256 + 156 */
#define UPDATE_ERASES 63
#define UPDATE_XFERS  28
#define UPDATE_CMDS   (3 + UPDATE_ERASES + UPDATE_XFERS)

static void touch_before(void *fixture) {
  ARG_UNUSED(fixture);
  aw9320x_Global_var_rst_def_val();
  g_aw9320x_func = &aw9320x_func;
  aw9320x_wearing = UNKNOWN;
  aw93203_ready = 0;
}

ZTEST_SUITE(touch, NULL, NULL, touch_before, NULL, NULL);

/* returns the number of chip writes so far */
static int init_ok(void) {
  zassert_equal(aw9320x_init(&aw9320x_func), 0, "aw9320x_init failed");
  return aw9320x_emul()->writes;
}

static void expect_write(int index, uint32_t reg, uint32_t val) {
  struct aw9320x_emul_data *chip = aw9320x_emul();

  zassert_true(index >= 0 && index < MIN(chip->writes, AW9320X_EMUL_WRITE_LOG), "write %d missing (%d writes)", index,
               chip->writes);
  zassert_equal(chip->write_addr[index], reg, "write %d: register %04x, expected %04x", index, chip->write_addr[index],
                reg);
  zassert_equal(chip->write_val[index], val, "write %d to %04x: %08x, expected %08x", index, reg,
                chip->write_val[index], val);
}

static void expect_cmd(int index, uint32_t module, uint32_t command, uint32_t seq, uint32_t addr, uint32_t length) {
  struct aw9320x_emul_cmd cmd = aw9320x_emul()->cmd_log[index];

  zassert_equal(cmd.module, module, "command %d: module %u", index, cmd.module);
  zassert_equal(cmd.command, command, "command %d: command %u", index, cmd.command);
  zassert_equal(cmd.seq, seq, "command %d: seq %u", index, cmd.seq);
  zassert_equal(cmd.addr, addr, "command %d: address %08x", index, cmd.addr);
  zassert_equal(cmd.length, length, "command %d: length %u", index, cmd.length);
  zassert_equal(cmd.status, 0, "command %d: status %u", index, cmd.status);
}

/* from write index on: the parameter table without IRQEN, active mode, IRQEN; returns the next */
static int expect_parameter_load(int index) {
  for (size_t entry = 0; entry < ARRAY_SIZE(aw9320x_reg_default); entry += 2) {
    if (aw9320x_reg_default[entry] == 0x4414) {
      continue;
    }
    expect_write(index++, aw9320x_reg_default[entry], aw9320x_reg_default[entry + 1]);
  }
  expect_write(index++, 0x4408, 1);
  expect_write(index++, 0x4414, TABLE_IRQEN);
  return index;
}

/* the update stopped after cmds boot loader commands and ended with these four writes */
static void expect_update_cleanup(int cmds) {
  int count = aw9320x_emul()->writes;

  zassert_equal(aw9320x_emul()->cmds, cmds);
  expect_write(count - 4, 0xff20, 0x3c00f091);
  expect_write(count - 3, 0x4748, 0);
  expect_write(count - 2, 0x4744, 0);
  expect_write(count - 1, 0xff20, 0x3c00f011);
}

static void expect_image_installed(void) {
  const uint8_t *flash = aw9320x_emul()->flash;

  zassert_mem_equal(&flash[0x2000], IMG_DATA, IMG_LEN, "image in flash differs");
  expect_fill(&flash[0x2000 + IMG_LEN], 0x3f80 - 0x2000 - IMG_LEN, 0xff, "after the image");
  zassert_equal(aw9320x_emul_flash_word(0x3ff8), AW9320X_EMUL_CHECK_EN);
  zassert_equal(aw9320x_emul_flash_word(0x3ffc), IMG_CHECK_CODE, "check code %08x", aw9320x_emul_flash_word(0x3ffc));
}

/* Soft reset 3Ch to FF18h, wrapped in access key (FF20h) and flash wake-up (4700h) writes. */
ZTEST(touch, test_init_resets_the_chip_and_loads_the_parameter_table) {
  init_ok();

  expect_write(0, 0xff20, 0x3c00f091);
  expect_write(1, 0x4700, 0x110);
  expect_write(2, 0x4444, 0);
  expect_write(3, 0xff18, 0x3c);
  expect_write(4, 0x4700, 0x102);
  /* 5 reset writes, 43 table entries, mode, IRQEN */
  zassert_equal(expect_parameter_load(5), 50);
  zassert_equal(aw9320x_emul()->writes, 50);
  zassert_equal(aw9320x_emul()->soft_resets, 1);
  zassert_equal(aw9320x_emul()->cmds, 0, "same firmware version: no update");
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_IRQEN), TABLE_IRQEN);
}

/* 1 + 20 + 25 ms of sleeps cover Tinit (20 ms) */
ZTEST(touch, test_init_waits_out_the_reset_without_polling) {
  int64_t start = g1_clock_uptime_ms();

  init_ok();
  expect_elapsed_ms(start, 46, 48);
}

ZTEST(touch, test_init_accepts_the_three_supported_chip_ids) {
  static const uint32_t ids[] = {0x20223203, 0x20223205, 0x20223207};

  for (size_t index = 0; index < ARRAY_SIZE(ids); index++) {
    aw9320x_emul()->chip_id = ids[index];
    zassert_equal(aw9320x_init(&aw9320x_func), 0, "chip id %08x rejected", ids[index]);
  }
}

ZTEST(touch, test_init_rejects_an_unknown_chip_id_before_writing_anything) {
  static const uint32_t ids[] = {0x00000000, 0x20223204, 0x12345678};

  for (size_t index = 0; index < ARRAY_SIZE(ids); index++) {
    aw9320x_emul()->chip_id = ids[index];
    zassert_equal(aw9320x_init(&aw9320x_func), -AW_ERR, "chip id %08x accepted", ids[index]);
  }
  zassert_equal(aw9320x_emul()->writes, 0);
  zassert_equal(aw9320x_emul()->soft_resets, 0);
}

ZTEST(touch, test_init_on_a_nak_reads_the_chip_id_three_times_then_fails) {
  int64_t start = g1_clock_uptime_ms();

  aw9320x_emul()->nak = true;
  zassert_equal(aw9320x_init(&aw9320x_func), -AW_ERR);
  zassert_equal(aw9320x_emul()->transfers, 3);
  expect_elapsed_ms(start, 3, 4);
}

ZTEST(touch, test_init_retries_a_transient_nak) {
  aw9320x_emul()->nak_remaining = 2;

  init_ok();
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
}

ZTEST(touch, test_init_without_the_init_interrupt_reinstalls_the_firmware) {
  aw9320x_emul_corrupt_fw();

  init_ok();
  zassert_equal(aw9320x_emul_count_writes(0x4408, 0x14), 1, "online upgrade command");
  zassert_equal(aw9320x_emul()->cmds, UPDATE_CMDS);
  expect_image_installed();
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
}

ZTEST(touch, test_port_init_sets_ready_only_on_success) {
  aw9320x_emul()->chip_id = 0x12345678;
  aw93203_init();
  zassert_equal(aw93203_ready, 0);

  aw9320x_emul()->chip_id = AW9320X_EMUL_CHIP_ID;
  aw93203_init();
  zassert_equal(aw93203_ready, 1);
}

ZTEST(touch, test_check_sum_accepts_the_shipped_image) {
  zassert_equal(sizeof(fw_para_data), 7140);
  zassert_equal(aw9320x_check_sum(fw_para_data, sizeof(fw_para_data)), AW_OK);
}

ZTEST(touch, test_check_sum_rejects_a_changed_byte) {
  static uint8_t img[sizeof(fw_para_data)];

  memcpy(img, fw_para_data, sizeof(img));
  img[72 + 1000] ^= 0x01;
  zassert_equal(aw9320x_check_sum(img, sizeof(img)), -AW_ERR);
}

ZTEST(touch, test_check_sum_is_a_byte_sum_stored_little_endian) {
  static const uint8_t le[] = {0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0xfe};
  static const uint8_t be[] = {0x00, 0x00, 0x01, 0x04, 0x01, 0x02, 0x03, 0xfe};

  /* 01h + 02h + 03h + FEh = 104h */
  zassert_equal(aw9320x_check_sum(le, sizeof(le)), AW_OK);
  zassert_equal(aw9320x_check_sum(be, sizeof(be)), -AW_ERR);
}

/* REG_CMD 2 sleep, 1 active */
ZTEST(touch, test_mode_set_sleep_and_active_write_cmd_and_get_reads_them_back) {
  int first = init_ok();

  aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
  expect_write(first, 0x4408, 2);
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_SLEEP_MODE);

  aw9320x_mode_operation_set(AW9320X_ACTIVE_MODE);
  expect_write(first + 1, 0x4408, 1);
  zassert_equal(aw9320x_emul()->writes, first + 2);
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_ACTIVE_MODE);
}

/* deep sleep needs the HOSTCTRL1 wake-up first */
ZTEST(touch, test_mode_set_leaving_deep_sleep_wakes_the_chip_first) {
  int first = init_ok();

  aw9320x_mode_operation_set(AW9320X_DEEPSLEEP_MODE);
  expect_write(first, 0x4408, 3);
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_DEEPSLEEP_MODE);

  aw9320x_mode_operation_set(AW9320X_ACTIVE_MODE);
  expect_write(first + 1, 0xff10, 1);
  expect_write(first + 2, 0x4408, 1);
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_ACTIVE_MODE);

  aw9320x_mode_operation_set(AW9320X_DEEPSLEEP_MODE);
  aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
  expect_write(first + 4, 0xff10, 1);
  expect_write(first + 5, 0x4408, 2);
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_SLEEP_MODE);
}

ZTEST(touch, test_mode_set_to_the_current_mode_writes_nothing) {
  int first = init_ok();

  aw9320x_mode_operation_set(AW9320X_ACTIVE_MODE);
  zassert_equal(aw9320x_emul()->writes, first);
}

ZTEST(touch, test_mode_set_waits_one_scan_period) {
  int64_t start;

  init_ok();
  start = g1_clock_uptime_ms();
  aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
  expect_elapsed_ms(start, 50, 51);
}

ZTEST(touch, test_mode_get_unknown_work_state_is_an_error) {
  init_ok();
  aw9320x_emul_set_reg(AW9320X_EMUL_REG_WST, 0x02000000);
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_MODE_ERR);
}

ZTEST(touch, test_mode_get_on_a_nak_is_an_error) {
  init_ok();
  aw9320x_mode_operation_set(AW9320X_SLEEP_MODE);
  aw9320x_emul()->nak = true;
  zassert_equal(aw9320x_mode_operation_get(), AW9320X_MODE_ERR);
}

/* the mask names the bits to keep: 00FFFFFFh replaces bits 31:24 */
ZTEST(touch, test_write_bits_replaces_only_the_bits_outside_the_mask) {
  aw9320x_emul_set_reg(0x4744, 0x01abcdef);

  zassert_equal(aw9320x_i2c_write_bits(0x4744, 0x00ffffff, 0), AW_OK);
  expect_write(0, 0x4744, 0x00abcdef);

  zassert_equal(aw9320x_i2c_write_bits(0x4744, 0x00ffffff, 0x5a123456), AW_OK);
  expect_write(1, 0x4744, 0x5aabcdef);
  zassert_equal(aw9320x_emul()->writes, 2);
  zassert_equal(aw9320x_emul_get_reg(0x4744), 0x5aabcdef);
}

/* the three read attempts are NAKed; a write after them would reach the chip */
ZTEST(touch, test_write_bits_on_a_failed_read_writes_nothing) {
  aw9320x_emul_set_reg(0x4744, 0x01abcdef);
  aw9320x_emul()->nak_remaining = 3;

  zassert_equal(aw9320x_i2c_write_bits(0x4744, 0x00ffffff, 0), -AW_ERR);
  zassert_equal(aw9320x_emul()->transfers, 3);
  zassert_equal(aw9320x_emul()->writes, 0);
  zassert_equal(aw9320x_emul_get_reg(0x4744), 0x01abcdef);
}

ZTEST(touch, test_pack_checksum_is_minus_the_sum_of_header_fields_and_words) {
  uint8_t erase[4] = {0x10, 0x00, 0x3f, 0x80};
  uint8_t check[16] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x3f, 0xf8, 0x20, 0x22, 0x20, 0x22, 0x29, 0xc8, 0x2e, 0x7d};

  /* -(1 + 1 + 0) */
  zassert_equal(get_pack_checksum(NULL, 0, 1, 1), 0xfffffffe);
  /* -(2 + 2 + 4 + 10003F80h) */
  zassert_equal(get_pack_checksum(erase, 4, 2, 2), 0xefffc078);
  /* -(1 + 2 + 16 + 0 + 10003FF8h + 20222022h + 29C82E7Dh) */
  zassert_equal(get_pack_checksum(check, 16, 1, 2), 0xa6157156);
}

/* header 0002h, size, checksum, module, command, length, value; MSB first */
ZTEST(touch, test_pack_send_writes_the_packet_at_0800_msb_first) {
  uint8_t addr[4] = {0x10, 0x00, 0x3f, 0x80};

  zassert_equal(dri_to_soc_pack_send(2, 2, sizeof(addr), addr), AW_OK);
  zassert_equal(aw9320x_emul()->transfers, 1);
  expect_bytes(&aw9320x_emul()->sram[0x0800], 16, "0002 0010 efffc078 0202 0004 10003f80");
}

ZTEST(touch, test_pack_send_tries_six_times_on_nak) {
  aw9320x_emul()->nak = true;
  zassert_equal(dri_to_soc_pack_send(1, 1, 0, NULL), -AW_ERR);
  zassert_equal(aw9320x_emul()->transfers, 6);
}

static void put_ack(uint8_t module, uint8_t command, uint32_t status) {
  uint8_t ack[20] = {0x00, 0x02, 0x00,         0x14,         0,           0,     0, 0, module, command,
                     0x00, 0x08, status >> 24, status >> 16, status >> 8, status};

  memcpy(&aw9320x_emul()->sram[0x1800], ack, sizeof(ack));
}

ZTEST(touch, test_pack_parse_accepts_the_ack_of_the_same_command) {
  put_ack(1, 1, 0);
  zassert_equal(soc_to_dri_pack_parse(8, 1, 1), AW_OK);
}

ZTEST(touch, test_pack_parse_rejects_another_module_or_command) {
  put_ack(1, 1, 0);
  zassert_equal(soc_to_dri_pack_parse(8, 2, 1), -AW_ERR);
  zassert_equal(soc_to_dri_pack_parse(8, 1, 2), -AW_ERR);
}

ZTEST(touch, test_pack_parse_rejects_a_non_zero_status) {
  put_ack(1, 1, 1);
  zassert_equal(soc_to_dri_pack_parse(8, 1, 1), -AW_ERR);
  put_ack(1, 1, 0x01000000);
  zassert_equal(soc_to_dri_pack_parse(8, 1, 1), -AW_ERR);
}

static int heap_blocks(void) {
  static void *blocks[4096];
  int count = 0;

  while (count < (int)ARRAY_SIZE(blocks) && (blocks[count] = malloc(64)) != NULL) {
    count++;
  }
  zassert_true(count < (int)ARRAY_SIZE(blocks), "heap larger than the probe");
  for (int index = 0; index < count; index++) {
    free(blocks[index]);
  }
  return count;
}

ZTEST(touch, test_pack_parse_failure_frees_its_buffer) {
  int before;

  put_ack(1, 1, 0);
  before = heap_blocks();
  for (int attempt = 0; attempt < 64; attempt++) {
    zassert_equal(soc_to_dri_pack_parse(8, 2, 2), -AW_ERR);
  }
  zassert_equal(heap_blocks(), before, "heap lost %d blocks of 64 bytes", before - heap_blocks());
}

ZTEST(touch, test_pack_round_trip_through_the_boot_loader) {
  uint8_t xfer[12] = {0, 0, 0, 1, 0x10, 0x00, 0x20, 0x00, 0xaa, 0xbb, 0xcc, 0xdd};
  struct aw9320x_emul_cmd cmd;

  zassert_equal(aw9320x_i2c_write(0x4408, 0x14), AW_OK);
  zassert_equal(dri_to_soc_pack_send(1, 2, sizeof(xfer), xfer), AW_OK);
  zassert_equal(aw9320x_i2c_write(0x1c10, 0x20000800), AW_OK);
  zassert_equal(aw9320x_i2c_write(0x4408, 0x15), AW_OK);
  zassert_equal(soc_to_dri_pack_parse(8, 1, 2), AW_OK);

  zassert_equal(aw9320x_emul()->cmds, 1);
  cmd = aw9320x_emul()->cmd_log[0];
  zassert_equal(cmd.status, 0, "packet rejected: status %u", cmd.status);
  zassert_equal(cmd.seq, 1);
  zassert_equal(cmd.addr, 0x10002000);
  zassert_equal(aw9320x_emul_flash_word(0x2000), 0xaabbccdd);
}

ZTEST(touch, test_fw_version_cmp_is_equal_only_for_the_same_version) {
  int8_t cmp = -1;

  zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), AW_OK);
  zassert_equal(cmp, AW9320X_VER_EQUAL);

  aw9320x_emul_set_fw_version(IMG_VERSION - 1);
  zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), AW_OK);
  zassert_equal(cmp, AW9320X_VER_NOT_EQUAL);

  aw9320x_emul_set_fw_version(IMG_VERSION + 1);
  cmp = -1;
  zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), AW_OK);
  zassert_equal(cmp, AW9320X_VER_NOT_EQUAL);
}

ZTEST(touch, test_fw_version_cmp_on_a_nak_fails_and_keeps_the_result) {
  int8_t cmp = 7;

  aw9320x_emul()->nak = true;
  zassert_equal(aw9320x_fw_version_cmp(&cmp, IMG_VERSION), -AW_ERR);
  zassert_equal(cmp, 7);
}

ZTEST(touch, test_fw_update_with_the_same_version_changes_nothing) {
  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
  zassert_equal(aw9320x_emul()->writes, 0);
  zassert_equal(aw9320x_emul()->cmds, 0);
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
}

ZTEST(touch, test_fw_update_sends_start_erase_check_words_erase_and_transfers_in_order) {
  int index = 0;

  aw9320x_emul_set_fw_version(OTHER_VERSION);

  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
  expect_write(0, 0x4408, 0x14);
  zassert_equal(aw9320x_emul()->cmds, UPDATE_CMDS);
  expect_cmd(index++, 1, 1, 0, 0, 0);
  expect_cmd(index++, 2, 2, 0, 0x10003f80, 4);
  expect_cmd(index++, 1, 2, 0, 0x10003ff8, 16);
  for (uint32_t sector = 0; sector < UPDATE_ERASES; sector++) {
    expect_cmd(index++, 2, 2, 0, 0x10002000 + sector * 128, 4);
  }
  for (uint32_t seq = 1; seq <= UPDATE_XFERS; seq++) {
    expect_cmd(index++, 1, 2, seq, 0x10002000 + (seq - 1) * 256, seq < UPDATE_XFERS ? 8 + 256 : 8 + 156);
  }
}

ZTEST(touch, test_fw_update_installs_the_image_and_check_words) {
  aw9320x_emul_set_fw_version(OTHER_VERSION);

  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
  expect_image_installed();
  zassert_equal(aw9320x_emul()->isp_ops[AW9320X_EMUL_ISP_READ], 3);
}

ZTEST(touch, test_fw_update_stops_the_boot_loader_and_waits_for_init) {
  int mcfg;

  aw9320x_emul_set_fw_version(OTHER_VERSION);

  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), AW_OK);
  mcfg = aw9320x_emul_find_write(0x4444, 0);
  zassert_true(mcfg > 0, "no MCFG write");
  expect_write(mcfg, 0x4444, 0x10000);
  expect_write(mcfg + 1, 0xff20, 0x3c00ffff);
  expect_write(mcfg + 2, 0x4744, 0);
  expect_write(mcfg + 3, 0xff20, 0x3c00f011);
  expect_write(mcfg + 4, 0x4444, 0);
  zassert_equal(aw9320x_emul()->writes, mcfg + 5);
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
  zassert_false(aw9320x_emul_int_asserted(), "init interrupt not read");
}

static uint16_t unreadable_reg;

/* the port's read, failing for unreadable_reg */
static int32_t i2c_read_unless_unreadable(uint8_t *addr, uint8_t addr_len, uint8_t *r_data, uint32_t r_len) {
  if (sys_get_be16(addr) == unreadable_reg) {
    return -1;
  }
  return aw93203_i2c_read(addr, addr_len, r_data, r_len);
}

/* UPDATA_DIS keeps blocking the boot jump: the CPU restarts into the boot loader, no init interrupt */
ZTEST(touch, test_stop_cmd_on_a_failed_updata_dis_read_leaves_it_restarts_the_cpu_and_fails) {
  static struct aw9320x_func func;

  aw9320x_emul_set_reg(0x4744, 0x00abcdef);
  zassert_equal(aw9320x_i2c_write(0x4408, 0x14), AW_OK);
  zassert_equal(aw9320x_emul_get_reg(0x4744), 0x01abcdef);
  /* the power-on init interrupt is still pending */
  aw9320x_emul_set_reg(AW9320X_EMUL_REG_IRQSRC, 0);
  func = aw9320x_func;
  func.i2c_func.i2c_r = i2c_read_unless_unreadable;
  unreadable_reg = 0x4744;
  g_aw9320x_func = &func;

  zassert_equal(aw9320x_send_stop_cmd(), -AW_ERR);

  zassert_equal(aw9320x_emul_find_write(0x4744, 0), -1, "UPDATA_DIS written after a failed read");
  zassert_equal(aw9320x_emul_get_reg(0x4744), 0x01abcdef);
  expect_write(3, 0xff20, 0x3c00f011);
  expect_write(4, 0x4444, 0);
  zassert_equal(aw9320x_emul()->writes, 5);
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_BOOT_LOADER);
}

ZTEST(touch, test_fw_update_without_an_acknowledge_fails_and_cleans_up) {
  aw9320x_emul_set_fw_version(OTHER_VERSION);
  aw9320x_emul()->ack_irq = false;

  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), -AW_PROT_UPDATE_ERR);
  expect_update_cleanup(0);
}

ZTEST(touch, test_fw_update_nak_in_the_middle_fails_and_cleans_up) {
  aw9320x_emul_set_fw_version(OTHER_VERSION);
  aw9320x_emul_nak_after_cmds(70, 3);

  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), -AW_PROT_UPDATE_ERR);
  expect_update_cleanup(70);
  zassert_mem_equal(&aw9320x_emul()->flash[0x2000], IMG_DATA, 4 * 256);
  zassert_equal(aw9320x_emul_flash_word(0x2000 + 4 * 256), 0xffffffff);
}

ZTEST(touch, test_fw_update_rejected_command_fails_and_cleans_up) {
  aw9320x_emul_set_fw_version(OTHER_VERSION);
  aw9320x_emul()->fail_cmd = 2;

  zassert_equal(aw9320x_fw_update(AW_COMPARE_VERSION_LOAD), -AW_PROT_UPDATE_ERR);
  expect_update_cleanup(3);
}

ZTEST(touch, test_fw_update_direct_mode_ignores_the_version) {
  zassert_equal(aw9320x_fw_update(AW_DIRECT_LOAD), AW_OK);
  zassert_equal(aw9320x_emul()->cmds, UPDATE_CMDS);
  expect_image_installed();
}

ZTEST(touch, test_fw_update_direct_mode_failure_is_a_plain_error) {
  aw9320x_emul()->fail_cmd = 0;
  zassert_equal(aw9320x_fw_update(AW_DIRECT_LOAD), -AW_ERR);
}

/* 112 sectors: 37F8h in 128-byte sectors */
ZTEST(touch, test_update_err_handle_installs_through_isp_and_restarts_the_chip) {
  zassert_equal(aw9320x_update_err_handle(), AW_OK);

  expect_write(0, 0xff20, 0x3c00ffff);
  expect_write(1, 0x4444, 0x10000);
  expect_write(2, 0x4820, 6);
  expect_write(3, 0x4794, 0x5a637955);
  zassert_equal(aw9320x_emul()->isp_ops[AW9320X_EMUL_ISP_ERASE], 112);
  zassert_equal(aw9320x_emul()->isp_ops[AW9320X_EMUL_ISP_PROGRAM], IMG_LEN / 4 + 2);
  zassert_equal(aw9320x_emul()->isp_ops[AW9320X_EMUL_ISP_READ], IMG_LEN / 4);
  expect_image_installed();
  expect_fill(&aw9320x_emul()->flash[0x0800], 0x2000 - 0x0800, 0xff, "flash from 0800h");
  zassert_equal(aw9320x_emul()->soft_resets, 1);
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
}

ZTEST(touch, test_init_recovers_from_a_failed_update_through_isp) {
  aw9320x_emul_set_fw_version(OTHER_VERSION);
  aw9320x_emul_nak_after_cmds(70, 3);

  init_ok();
  zassert_equal(aw9320x_emul()->isp_ops[AW9320X_EMUL_ISP_PROGRAM], IMG_LEN / 4 + 2);
  expect_image_installed();
  zassert_equal(aw9320x_emul()->state, AW9320X_EMUL_APP);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_WST), AW9320X_EMUL_WST_ACTIVE);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_IRQEN), TABLE_IRQEN);
}

ZTEST(touch, test_irq_stat_proximity_is_in_ear) {
  init_ok();
  zassert_true(aw9320x_emul_touch(true));
  zassert_true(aw9320x_emul_int_asserted());

  aw9320x_get_irq_stat();
  zassert_equal(aw9320x_wearing, IN_EAR);
  zassert_equal(aw9320x_emul_get_reg(AW9320X_EMUL_REG_IRQSRC), 0);
  zassert_false(aw9320x_emul_int_asserted());
}

ZTEST(touch, test_irq_stat_proximity_exit_is_out_of_ear) {
  init_ok();
  aw9320x_emul_touch(true);
  aw9320x_get_irq_stat();
  zassert_true(aw9320x_emul_touch(false));

  aw9320x_get_irq_stat();
  zassert_equal(aw9320x_wearing, OUT_EAR);
  zassert_false(aw9320x_emul_int_asserted());
}

ZTEST(touch, test_irq_stat_init_interrupt_reloads_the_parameters) {
  int index;

  init_ok();
  zassert_equal(aw9320x_i2c_write(0xff18, 0x3c), AW_OK);
  g1_clock_advance_ms(AW9320X_EMUL_TINIT_MS);
  zassert_true(aw9320x_emul_int_asserted());
  zassert_false(aw9320x_emul_touch(true), "touch reported without configuration");
  aw9320x_emul_touch(false);
  index = aw9320x_emul()->writes;

  aw9320x_get_irq_stat();
  index = expect_parameter_load(index);
  zassert_equal(aw9320x_emul()->writes, index);
  zassert_true(aw9320x_emul_touch(true));
}

ZTEST(touch, test_irq_stat_on_a_nak_changes_nothing) {
  init_ok();
  aw9320x_emul_touch(true);
  aw9320x_emul()->nak = true;

  aw9320x_get_irq_stat();
  zassert_equal(aw9320x_wearing, UNKNOWN);
  aw9320x_emul()->nak = false;
  zassert_true(aw9320x_emul_int_asserted(), "source lost");
}

ZTEST(touch, test_diff_get_drops_the_10_fraction_bits) {
  int32_t diff = 1;

  init_ok();
  aw9320x_emul()->touch_diff = -25000 * 1024;
  aw9320x_emul_touch(true);
  aw9320x_diff_get(&diff);
  zassert_equal(diff, -25000);

  aw9320x_emul_touch(false);
  aw9320x_diff_get(&diff);
  zassert_equal(diff, 0);
}
