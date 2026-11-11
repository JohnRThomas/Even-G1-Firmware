/*
 * ST25DV04KC NFC tag emulator; either node's emul drives the same chip. Getters have no side
 * effects (reading IT_STS_Dyn here does not clear it). The chip NAKs while programming EEPROM;
 * g1_clock_advance_ms() ends it. g1_unit_reboot() keeps the EEPROM (user memory, static registers,
 * password).
 */
#ifndef G1_UNIT_ST25DV_EMUL_H_
#define G1_UNIT_ST25DV_EMUL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

#define G1_ST25DV_EMUL     EMUL_DT_GET(DT_NODELABEL(st25dv))
#define G1_ST25DV_SYS_EMUL EMUL_DT_GET(DT_NODELABEL(st25dv_sys))

#define ST25DV_EMUL_ADDR_USER 0x53
#define ST25DV_EMUL_ADDR_SYS  0x57

#define ST25DV_EMUL_USER_MAX  8192 /* ST25DV64KC */
#define ST25DV_EMUL_MB_SIZE   256
#define ST25DV_EMUL_LOG       64
#define ST25DV_EMUL_LOG_BYTES 40

#define ST25DV_EMUL_SYS_GPO1        0x0000
#define ST25DV_EMUL_SYS_GPO2        0x0001
#define ST25DV_EMUL_SYS_EH_MODE     0x0002
#define ST25DV_EMUL_SYS_RF_MNGT     0x0003
#define ST25DV_EMUL_SYS_I2CSS       0x000b
#define ST25DV_EMUL_SYS_FTM         0x000d
#define ST25DV_EMUL_SYS_I2C_CFG     0x000e
#define ST25DV_EMUL_SYS_MEM_SIZE    0x0014 /* 2 bytes, LSB first */
#define ST25DV_EMUL_SYS_BLK_SIZE    0x0016
#define ST25DV_EMUL_SYS_IC_REF      0x0017
#define ST25DV_EMUL_SYS_UID         0x0018 /* 8 bytes, LSB first */
#define ST25DV_EMUL_SYS_IC_REV      0x0020
#define ST25DV_EMUL_SYS_I2C_PWD     0x0900

#define ST25DV_EMUL_DYN_GPO_CTRL    0x2000
#define ST25DV_EMUL_DYN_EH_CTRL     0x2002
#define ST25DV_EMUL_DYN_RF_MNGT     0x2003
#define ST25DV_EMUL_DYN_I2C_SSO     0x2004
#define ST25DV_EMUL_DYN_IT_STS      0x2005
#define ST25DV_EMUL_DYN_MB_CTRL     0x2006
#define ST25DV_EMUL_DYN_MB_LEN      0x2007
#define ST25DV_EMUL_MAILBOX         0x2008

#define ST25DV_EMUL_GPO1_GPO_EN          0x01
#define ST25DV_EMUL_GPO1_FIELD_CHANGE_EN 0x10
#define ST25DV_EMUL_GPO1_RF_PUT_MSG_EN   0x20
#define ST25DV_EMUL_GPO1_RF_GET_MSG_EN   0x40
#define ST25DV_EMUL_GPO2_I2C_WRITE_EN    0x01
/* EH_CTRL_Dyn */
#define ST25DV_EMUL_EH_EN       0x01
#define ST25DV_EMUL_EH_ON       0x02
#define ST25DV_EMUL_FIELD_ON    0x04
#define ST25DV_EMUL_VCC_ON      0x08
/* IT_STS_Dyn */
#define ST25DV_EMUL_IT_FIELD_FALLING 0x08
#define ST25DV_EMUL_IT_FIELD_RISING  0x10
#define ST25DV_EMUL_IT_RF_PUT_MSG    0x20
#define ST25DV_EMUL_IT_RF_GET_MSG    0x40
/* MB_CTRL_Dyn */
#define ST25DV_EMUL_MB_EN           0x01
#define ST25DV_EMUL_HOST_PUT_MSG    0x02
#define ST25DV_EMUL_RF_PUT_MSG      0x04
#define ST25DV_EMUL_HOST_MISS_MSG   0x10
#define ST25DV_EMUL_RF_MISS_MSG     0x20
#define ST25DV_EMUL_HOST_CURRENT_MSG 0x40
#define ST25DV_EMUL_RF_CURRENT_MSG  0x80
#define ST25DV_EMUL_FTM_MB_MODE     0x01

#define ST25DV_EMUL_IC_REF_04KC     0x50
#define ST25DV_EMUL_IC_REV          0x11 /* revision-dependent */
#define ST25DV_EMUL_WRITE_TIME_MS   5    /* tW maximum */

struct st25dv_emul_xfer {
	uint8_t addr;                         /* 7-bit */
	uint16_t wr_len;                      /* address bytes included */
	uint8_t wr[ST25DV_EMUL_LOG_BYTES];
	uint16_t rd_len;
	int ret;                              /* 0 or -EIO (NAK) */
};

void st25dv_emul_rf_field(const struct emul *target, bool on);
int st25dv_emul_rf_set_mb_en(const struct emul *target, bool on);
int st25dv_emul_rf_put_msg(const struct emul *target, const uint8_t *data, size_t len);
int st25dv_emul_rf_get_msg(const struct emul *target, uint8_t *buf, size_t cap);
void st25dv_emul_enable_mailbox(const struct emul *target);

void st25dv_emul_set_ic_ref(const struct emul *target, uint8_t v);
void st25dv_emul_set_uid(const struct emul *target, const uint8_t uid[8]);
void st25dv_emul_set_ic_rev(const struct emul *target, uint8_t v);
void st25dv_emul_set_mem_size(const struct emul *target, uint16_t mem_size, uint8_t blk_size);

void st25dv_emul_set_nak(const struct emul *target, bool nak);
void st25dv_emul_fail_next(const struct emul *target, int n);
void st25dv_emul_set_write_time_ms(const struct emul *target, uint32_t ms);

uint8_t *st25dv_emul_user_mem(const struct emul *target);
size_t st25dv_emul_user_size(const struct emul *target);
uint8_t st25dv_emul_get_sys(const struct emul *target, uint16_t addr);
void st25dv_emul_set_sys(const struct emul *target, uint16_t addr, uint8_t v);
uint8_t st25dv_emul_get_dyn(const struct emul *target, uint16_t addr);
const uint8_t *st25dv_emul_mailbox(const struct emul *target);
bool st25dv_emul_session_open(const struct emul *target);
void st25dv_emul_set_password(const struct emul *target, const uint8_t pwd[8]);
int st25dv_emul_gpo_pulses(const struct emul *target);
void st25dv_emul_connect_gpo(const struct emul *target, const struct device *port, uint8_t pin);
int st25dv_emul_xfers(const struct emul *target);
const struct st25dv_emul_xfer *st25dv_emul_xfer(const struct emul *target, int i);

#endif /* G1_UNIT_ST25DV_EMUL_H_ */
