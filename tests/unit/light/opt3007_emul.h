/*
 * OPT3001-compatible light sensor emulator; time follows g1_clock_advance_ms(). Tests read and set
 * its registers and logs directly in opt3007_emul. A conversion that ends shows there only at the
 * next transfer or opt3007_emul_set_result(). Every test starts at the power-on state.
 */
#ifndef G1_UNIT_OPT3007_EMUL_H_
#define G1_UNIT_OPT3007_EMUL_H_

#include "harness.h"

#define OPT3007_EMUL_REG_RESULT     0x00
#define OPT3007_EMUL_REG_CONFIG     0x01
#define OPT3007_EMUL_REG_LOW_LIMIT  0x02
#define OPT3007_EMUL_REG_HIGH_LIMIT 0x03
#define OPT3007_EMUL_REG_MFG_ID     0x7e
#define OPT3007_EMUL_REG_DEVICE_ID  0x7f

#define OPT3007_EMUL_CONFIG_RESET 0xc810
#define OPT3007_EMUL_MFG_ID       0x5449
#define OPT3007_EMUL_DEVICE_ID    0x3001

/* AUTO_FIRST_MS: extra range assessment of the first auto-range conversion */
#define OPT3007_EMUL_CONV_100_MS  100
#define OPT3007_EMUL_CONV_800_MS  800
#define OPT3007_EMUL_AUTO_FIRST_MS 10

#define OPT3007_EMUL_WRITE_LOG 16

struct opt3007_emul_data {
  uint16_t result;
  uint16_t config;
  uint16_t low_limit;
  uint16_t high_limit;
  uint16_t mfg_id;
  uint16_t device_id;
  uint8_t pointer;

  /* set by opt3007_emul_set_result() */
  bool fixed_result;
  uint16_t fixed;
  /* every transfer fails with -EIO */
  bool nak;
  int64_t conv_end; /* uptime ms, -1: none */

  int transfers; /* NAKed ones too */
  int writes;    /* the first OPT3007_EMUL_WRITE_LOG are logged */
  uint8_t write_reg[OPT3007_EMUL_WRITE_LOG];
  uint16_t write_val[OPT3007_EMUL_WRITE_LOG];
};

extern struct opt3007_emul_data opt3007_emul;

/* the result of every later conversion */
void opt3007_emul_set_result(uint16_t result);

#endif /* G1_UNIT_OPT3007_EMUL_H_ */
