/*
 * AW93203 touch / wear controller emulator on the aw93203 node. aw9320x_emul() is its state: tests
 * set the fault knobs and read the logs there. It first completes an init that is due; init after
 * any reset ends AW9320X_EMUL_TINIT_MS later, ISP and boot loader commands complete at once.
 * aw9320x_emul_touch() latches only in active mode with the IRQEN bit set, and returns whether it
 * did. aw9320x_emul_corrupt_fw() fails every boot until the firmware is rewritten.
 */
#ifndef G1_UNIT_AW9320X_EMUL_H_
#define G1_UNIT_AW9320X_EMUL_H_

#include "harness.h"

#define AW9320X_EMUL_REG_FIRMWARE    0x1ba8
#define AW9320X_EMUL_REG_WST         0x1bac
#define AW9320X_EMUL_REG_ERR_CODE    0x1c00
#define AW9320X_EMUL_REG_DIFF_CH0    0x1c04
#define AW9320X_EMUL_REG_CMD_PACK    0x1c10
#define AW9320X_EMUL_REG_BT_VERSION  0x1c30
#define AW9320X_EMUL_REG_BT_DATE     0x1c34
#define AW9320X_EMUL_REG_CMD         0x4408
#define AW9320X_EMUL_REG_IRQSRC      0x4410
#define AW9320X_EMUL_REG_IRQEN       0x4414
#define AW9320X_EMUL_REG_MCFG        0x4444
#define AW9320X_EMUL_REG_ISPADR      0x4704
#define AW9320X_EMUL_REG_ISPWDATA    0x4708
#define AW9320X_EMUL_REG_ISPRDATA    0x470c
#define AW9320X_EMUL_REG_ISPCMD      0x4710
#define AW9320X_EMUL_REG_ISPGO       0x4714
#define AW9320X_EMUL_REG_UPDATA_DIS  0x4744 /* bits 31:24 block the boot jump */
#define AW9320X_EMUL_REG_BL_ACTIVE   0x4748
#define AW9320X_EMUL_REG_CHIPID      0xff00
#define AW9320X_EMUL_REG_HOSTCTRL1   0xff10
#define AW9320X_EMUL_REG_RSTNALL     0xff18

#define AW9320X_EMUL_IRQ_INIT        (1u << 0)
#define AW9320X_EMUL_IRQ_PROX        (1u << 1)
#define AW9320X_EMUL_IRQ_PROX_EXIT   (1u << 2)
#define AW9320X_EMUL_IRQ_ACK         (1u << 29)

#define AW9320X_EMUL_CMD_ACTIVE      1
#define AW9320X_EMUL_CMD_SLEEP       2
#define AW9320X_EMUL_CMD_DEEPSLEEP   3
#define AW9320X_EMUL_CMD_ONLINE_UPGRADE 0x14 /* enter the boot loader */
#define AW9320X_EMUL_CMD_PARSE       0x15 /* execute the command packet */

#define AW9320X_EMUL_WST_ACTIVE      0x01000000
#define AW9320X_EMUL_WST_SLEEP       0x03000000
#define AW9320X_EMUL_WST_DEEPSLEEP   0x00000000

#define AW9320X_EMUL_SRAM_SIZE       0x2000
#define AW9320X_EMUL_SRAM_BASE       0x20000000
#define AW9320X_EMUL_ACK_ADDR        0x1800

/* boot loader: FLASH_BASE + offset */
#define AW9320X_EMUL_FLASH_SIZE      0x4000
#define AW9320X_EMUL_FLASH_BASE      0x10000000
#define AW9320X_EMUL_SECTOR_SIZE     128
#define AW9320X_EMUL_CHECK_EN_ADDR   0x3ff8
#define AW9320X_EMUL_CHECK_EN        0x20222022

#define AW9320X_EMUL_ISP_READ        0x03
#define AW9320X_EMUL_ISP_ERASE       0x05
#define AW9320X_EMUL_ISP_PROGRAM     0x0c

#define AW9320X_EMUL_CHIP_ID         0x20223203
#define AW9320X_EMUL_FW_VERSION      0x06000007
#define AW9320X_EMUL_BT_VERSION      0x01000000
#define AW9320X_EMUL_BT_DATE         0x20220101
#define AW9320X_EMUL_TOUCH_DIFF      (30000 << 10)

/* datasheet Tinit */
#define AW9320X_EMUL_TINIT_MS        20

/* any non-zero status is an error to the driver */
#define AW9320X_EMUL_STATUS_BAD_PACKET 1
#define AW9320X_EMUL_STATUS_BAD_ADDR   2
#define AW9320X_EMUL_STATUS_UNKNOWN    3
#define AW9320X_EMUL_STATUS_INJECTED   4

#define AW9320X_EMUL_WRITE_LOG       20000
#define AW9320X_EMUL_CMD_LOG         128

enum aw9320x_emul_state {
  AW9320X_EMUL_APP,
  AW9320X_EMUL_INIT,        /* init running, ends Tinit later */
  AW9320X_EMUL_BOOT_LOADER,
  AW9320X_EMUL_BOOT_FAILED, /* firmware failed verification */
  AW9320X_EMUL_HALTED,      /* MCFG bit 16 */
};

struct aw9320x_emul_cmd {
  uint8_t module;
  uint8_t command;
  uint16_t length;
  uint32_t seq;    /* transfer: sequence number, else 0 */
  uint32_t addr;
  uint32_t status;
};

struct aw9320x_emul_data {
  uint8_t sram[AW9320X_EMUL_SRAM_SIZE];
  uint32_t sfr[0x1000 / 4]; /* registers from 4000h */
  uint32_t sys[0x100 / 4];  /* registers from FF00h */
  uint8_t flash[AW9320X_EMUL_FLASH_SIZE];
  uint32_t irqsrc;
  uint32_t chip_id;
  uint32_t fw_version;      /* set with aw9320x_emul_set_fw_version() */
  uint16_t pointer;

  enum aw9320x_emul_state state;
  int64_t init_due;         /* -1: none */
  uint32_t mode;
  bool host_wake;           /* since entering deep sleep */
  bool touched;
  int32_t touch_diff;
  bool int_low;

  bool nak;                 /* NAK every transfer */
  int nak_remaining;        /* NAK this many transfers next */
  bool nak_after_armed;     /* set with aw9320x_emul_nak_after_cmds() */
  int nak_after_cmds;
  int nak_after_count;
  int fail_cmd;             /* index of the command to reject, -1: none */
  bool ack_irq;

  int transfers;
  int writes;
  uint16_t write_addr[AW9320X_EMUL_WRITE_LOG];
  uint32_t write_val[AW9320X_EMUL_WRITE_LOG];
  int cmds;                 /* boot loader commands executed */
  struct aw9320x_emul_cmd cmd_log[AW9320X_EMUL_CMD_LOG];
  int isp_ops[256];         /* by ISP command */
  int soft_resets;
};

struct aw9320x_emul_data *aw9320x_emul(void);

void aw9320x_emul_set_fw_version(uint32_t version);
void aw9320x_emul_corrupt_fw(void);
void aw9320x_emul_nak_after_cmds(int cmds, int count);

bool aw9320x_emul_touch(bool down);
bool aw9320x_emul_int_asserted(void);
void aw9320x_emul_sync_int(void);

uint32_t aw9320x_emul_get_reg(uint16_t addr);
void aw9320x_emul_set_reg(uint16_t addr, uint32_t val);
uint32_t aw9320x_emul_flash_word(uint32_t addr);
int aw9320x_emul_find_write(uint16_t addr, int from);
int aw9320x_emul_count_writes(uint16_t addr, uint32_t val);

#endif /* G1_UNIT_AW9320X_EMUL_H_ */
