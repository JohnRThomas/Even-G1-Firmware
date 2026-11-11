/*
 * OPT3001-compatible light sensor emulator; time follows g1_clock_advance_ms(). get_reg() does not
 * clear CRF.
 */
#ifndef G1_UNIT_OPT3007_EMUL_H_
#define G1_UNIT_OPT3007_EMUL_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/emul.h>

#define G1_OPT3007_EMUL EMUL_DT_GET(DT_NODELABEL(opt3007))

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

void opt3007_emul_set_lux_centi(const struct emul *target, uint32_t centilux);
void opt3007_emul_set_result(const struct emul *target, uint16_t result);
void opt3007_emul_set_ids(const struct emul *target, uint16_t mfg_id, uint16_t device_id);
void opt3007_emul_set_nak(const struct emul *target, bool nak);
uint16_t opt3007_emul_get_reg(const struct emul *target, uint8_t reg);
void opt3007_emul_set_reg(const struct emul *target, uint8_t reg, uint16_t val);
int opt3007_emul_writes(const struct emul *target);
uint8_t opt3007_emul_write_reg(const struct emul *target, int i);
uint16_t opt3007_emul_write_val(const struct emul *target, int i);
int opt3007_emul_transfers(const struct emul *target);

#endif /* G1_UNIT_OPT3007_EMUL_H_ */
