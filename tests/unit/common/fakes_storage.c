/* RAM NOR flash (erased on reset, kept on reboot) and flash_settings.c fakes. */
#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>

#include "fakes.h"

#include "storage/storage.h"

uint8_t g1_fake_flash_image[G1_FAKE_FLASH_SIZE];

DEFINE_FAKE_VALUE_FUNC(int, g1_fake_flash_read, struct GlassesState *, uint32_t, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_flash_write, struct GlassesState *, uint32_t, const void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_flash_erase, struct GlassesState *, uint32_t, size_t);

DEFINE_FAKE_VALUE_FUNC(int, flash_settings_read, uint32_t, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, flash_settings_write_and_verify, uint32_t, const void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, write_whitelist_to_flash, uint32_t, const uint8_t *, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, write_font, uint32_t, const void *, size_t);

DEFINE_FAKE_VOID_FUNC(storage_commit_request, int, uint8_t);
__weak struct k_sem flash_ops_sem;
__weak uint8_t sys_settings_dirty;
__weak uint32_t LAST_FILE_CRC;

/* GlassesState->store.write: 0, a store with room for every setting */
FAKE_VALUE_FUNC(int, g1_fake_store_write, const char *, const void *, size_t);

static bool in_range(uint32_t addr, size_t len) {
  return addr <= G1_FAKE_FLASH_SIZE && len <= G1_FAKE_FLASH_SIZE - addr;
}

static int ram_read(struct GlassesState *state, uint32_t addr, void *buf, size_t len) {
  ARG_UNUSED(state);
  if (!in_range(addr, len)) {
    return -EINVAL;
  }
  memcpy(buf, &g1_fake_flash_image[addr], len);
  return 0;
}

/* NOR: a write only clears bits */
static int ram_write(struct GlassesState *state, uint32_t addr, const void *buf, size_t len) {
  const uint8_t *bytes = buf;

  ARG_UNUSED(state);
  if (!in_range(addr, len)) {
    return -EINVAL;
  }
  for (size_t offset = 0; offset < len; offset++) {
    g1_fake_flash_image[addr + offset] &= bytes[offset];
  }
  return 0;
}

static int ram_erase(struct GlassesState *state, uint32_t addr, size_t len) {
  ARG_UNUSED(state);
  if (!in_range(addr, len)) {
    return -EINVAL;
  }
  memset(&g1_fake_flash_image[addr], 0xff, len);
  return 0;
}

static int settings_read_default(uint32_t addr, void *buf, size_t len) {
  return ram_read(NULL, addr, buf, len);
}

/*
 * Rewrites the sector at addr with data unless it already starts with it; pad_sector writes the
 * whole sector, zero-padded. Data longer than a sector or a sector out of range is -EINVAL.
 */
static int rewrite_sector(uint32_t addr, const void *data, size_t len, bool pad_sector) {
  static uint8_t sector[G1_FAKE_FLASH_SECTOR];
  int ret;

  if (len > G1_FAKE_FLASH_SECTOR || !in_range(addr, G1_FAKE_FLASH_SECTOR)) {
    return -EINVAL;
  }
  if (memcmp(data, &g1_fake_flash_image[addr], len) == 0) {
    return 0;
  }
  if (pad_sector) {
    memset(sector, 0, sizeof(sector));
    memcpy(sector, data, len);
    data = sector;
    len = sizeof(sector);
  }
  ret = ram_erase(NULL, addr, G1_FAKE_FLASH_SECTOR);
  if (ret == 0) {
    ret = ram_write(NULL, addr, data, len);
  }
  return ret;
}

static int settings_write_default(uint32_t addr, const void *data, size_t len) {
  return rewrite_sector(addr, data, len, true);
}

static int font_write_default(uint32_t addr, const void *data, size_t len) {
  return rewrite_sector(addr, data, len, false);
}

static int whitelist_write_default(uint32_t addr, const uint8_t *data, uint32_t size) {
  uint8_t pages = (size >> 12) + 1;
  int16_t last = size - (pages - 1) * G1_FAKE_FLASH_SECTOR;
  int ret = 0;

  for (uint8_t page = 0; page < pages; page++) {
    uint32_t offset = page * G1_FAKE_FLASH_SECTOR;
    size_t len = page < pages - 1 ? G1_FAKE_FLASH_SECTOR : last;

    ret += flash_settings_write_and_verify(addr + offset, data + offset, len);
  }
  return ret;
}

static void fakes_storage_reset(bool keep_media) {
  struct GlassesState *state = get_dashboard_state();

  if (!keep_media) {
    memset(g1_fake_flash_image, 0xff, sizeof(g1_fake_flash_image));
  }

  RESET_FAKE(g1_fake_flash_read);
  RESET_FAKE(g1_fake_flash_write);
  RESET_FAKE(g1_fake_flash_erase);
  g1_fake_flash_read_fake.custom_fake = ram_read;
  g1_fake_flash_write_fake.custom_fake = ram_write;
  g1_fake_flash_erase_fake.custom_fake = ram_erase;

  RESET_FAKE(flash_settings_read);
  RESET_FAKE(flash_settings_write_and_verify);
  RESET_FAKE(write_whitelist_to_flash);
  RESET_FAKE(write_font);
  flash_settings_read_fake.custom_fake = settings_read_default;
  flash_settings_write_and_verify_fake.custom_fake = settings_write_default;
  write_whitelist_to_flash_fake.custom_fake = whitelist_write_default;
  write_font_fake.custom_fake = font_write_default;

  RESET_FAKE(storage_commit_request);

  /* as init_context() sets them */
  state->ext_flash_ok = 0;
  state->flash_page_count = G1_FAKE_FLASH_SIZE / G1_FAKE_FLASH_SECTOR;
  state->flash_read = g1_fake_flash_read;
  state->flash_write = g1_fake_flash_write;
  state->flash_erase = g1_fake_flash_erase;
  state->store.write = g1_fake_store_write;
}

G1_UNIT_RESET_HOOK(fakes_storage_reset);
