/* Record addresses hard-coded so a moved record fails. */
#ifndef PERSISTENCE_H_
#define PERSISTENCE_H_

#include "harness.h"
#include "fakes.h"
#include "storage/storage.h"

#define FLASH_SYS_SETTINGS 0x130000
#define FLASH_USR_SETTINGS 0x134000
#define FLASH_BURIAL_POINT 0x135000
#define FLASH_SETTING      0x13e000
#define FLASH_QUICKNOTES   0x403000

#define SYS_SETTINGS_LEN 0x98
#define BURIAL_POINT_LEN 0x11c
#define QUICKNOTE_LEN    0x200

#define MAX_RECORD 0x400

/* the flash from addr on holds the bytes of hex */
static inline void expect_flash(uint32_t addr, const char *hex)
{
  static uint8_t want[MAX_RECORD];
  int len = g1_hex(hex, want, sizeof(want));

  zassert_true(len > 0, "empty hex");
  expect_bytes_msg(&g1_fake_flash_image[addr], len, hex, "flash 0x%06x", addr);
}

/* every byte of the flash from addr to addr + len is value */
#define expect_flash_fill(addr, len, value)                                                        \
  expect_fill(&g1_fake_flash_image[addr], (len), (value), "flash at " #addr)

static inline void put_flash(uint32_t addr, const char *hex)
{
  zassert_true(g1_hex(hex, &g1_fake_flash_image[addr], MAX_RECORD) > 0, "empty hex");
}

/* stands in for flash_ops_thread(); returns the wake-up count */
static inline int run_flash_ops_thread(void)
{
  int wakeups = 0;

  while (k_sem_take(&flash_ops_sem, K_NO_WAIT) == 0) {
    wakeups++;
    while (getQuickNoteQueueNum() != 0) {
      QuickNoteStoreHandler();
    }
    while (getSettingStorageQueueNum()) {
      SettingStoreHandler();
    }
  }
  return wakeups;
}

#endif /* PERSISTENCE_H_ */
