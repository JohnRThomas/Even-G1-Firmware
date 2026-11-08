/*
 * ST25DV wrapped as a Zephyr "eeprom" device (eeprom_st25dv.c): user memory,
 * system area and mailbox RAM access over I2C.
 */
#ifndef G1_DRIVERS_NFC_EEPROM_ST25DV_H_
#define G1_DRIVERS_NFC_EEPROM_ST25DV_H_

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <zephyr/device.h>

/* the eeprom device and a pointer to it (set by nfc_init()) */
extern struct device global_st25dv_context;
extern const struct device *global_st25dv_context_ptr;

int eeprom_st25dv_read(const struct device *dev, off_t offset, void *buf, size_t len);
int eeprom_st25dv_write(const struct device *dev, off_t offset, const void *buf, size_t len);
int eeprom_st25dv_read_sys(const struct device *dev, off_t offset, void *buf, size_t len);
int eeprom_st25dv_write_sys(const struct device *dev, off_t offset, const void *buf, size_t len);
int eeprom_st25dv_read_i2c_mb_mailbox_ram(const struct device *dev, uint8_t **buf, uint32_t *len);
int eeprom_st25dv_write_i2c_mb_mailbox_ram(const struct device *dev, const void *buf, size_t len);
const struct device *eeprom_st25dv_init(void);
uint8_t eeprom_st25dv_lock(const struct device *dev);
uint8_t eeprom_st25dv_unlock(const struct device *dev);

#endif /* G1_DRIVERS_NFC_EEPROM_ST25DV_H_ */
