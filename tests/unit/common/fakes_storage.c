#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>
#include <string.h>

#include "fakes_storage.h"
#include "harness.h"

#include "notify/demo_image.h"
#include "storage/storage.h"

uint8_t g1_fake_flash_image[G1_FAKE_FLASH_SIZE];

DEFINE_FAKE_VALUE_FUNC(int, g1_fake_flash_read, struct GlassesState *, uint32_t, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_flash_write, struct GlassesState *, uint32_t, const void *,
		       size_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_flash_erase, struct GlassesState *, uint32_t, size_t);

DEFINE_FAKE_VALUE_FUNC(int, flash_settings_read, uint32_t, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, flash_settings_write_and_verify, uint32_t, const void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, write_whitelist_to_flash, uint32_t, const uint8_t *, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, write_font, uint32_t, const void *, size_t);

DEFINE_FAKE_VOID_FUNC(get_demo_image_source_from_flash);

DEFINE_FAKE_VOID_FUNC(storage_commit_request, int, uint8_t);
__weak struct k_sem flash_ops_sem;
__weak uint8_t sys_settings_dirty;
__weak uint32_t LAST_FILE_CRC;

DEFINE_FAKE_VALUE_FUNC(int, g1_fake_store_write, const char *, const void *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_store_load);
DEFINE_FAKE_VOID_FUNC(g1_fake_store_del, const char *);

static bool in_range(uint32_t addr, size_t len)
{
	return addr <= G1_FAKE_FLASH_SIZE && len <= G1_FAKE_FLASH_SIZE - addr;
}

static int ram_read(struct GlassesState *gs, uint32_t addr, void *buf, size_t len)
{
	ARG_UNUSED(gs);
	if (!in_range(addr, len)) {
		return -EINVAL;
	}
	memcpy(buf, &g1_fake_flash_image[addr], len);
	return 0;
}

static int ram_write(struct GlassesState *gs, uint32_t addr, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	ARG_UNUSED(gs);
	if (!in_range(addr, len)) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		g1_fake_flash_image[addr + i] &= p[i];
	}
	return 0;
}

static int ram_erase(struct GlassesState *gs, uint32_t addr, size_t len)
{
	ARG_UNUSED(gs);
	if (!in_range(addr, len)) {
		return -EINVAL;
	}
	memset(&g1_fake_flash_image[addr], 0xff, len);
	return 0;
}

static int settings_read_default(uint32_t addr, void *buf, size_t len)
{
	return ram_read(NULL, addr, buf, len);
}

static int settings_write_default(uint32_t addr, const void *data, size_t len)
{
	static uint8_t sector[G1_FAKE_FLASH_SECTOR];
	int ret;

	if (len > G1_FAKE_FLASH_SECTOR || !in_range(addr, G1_FAKE_FLASH_SECTOR)) {
		return -EINVAL;
	}
	if (memcmp(data, &g1_fake_flash_image[addr], len) == 0) {
		return 0;
	}
	memset(sector, 0, sizeof(sector));
	memcpy(sector, data, len);
	ret = ram_erase(NULL, addr, G1_FAKE_FLASH_SECTOR);
	if (ret == 0) {
		ret = ram_write(NULL, addr, sector, G1_FAKE_FLASH_SECTOR);
	}
	return ret;
}

static int whitelist_write_default(uint32_t addr, const uint8_t *data, uint32_t size)
{
	uint8_t pages = (size >> 12) + 1;
	int16_t last = size - (pages - 1) * G1_FAKE_FLASH_SECTOR;
	int ret = 0;

	for (uint8_t i = 0; i < pages; i++) {
		ret += flash_settings_write_and_verify(addr + i * G1_FAKE_FLASH_SECTOR,
						       data + i * G1_FAKE_FLASH_SECTOR,
						       i < pages - 1 ? G1_FAKE_FLASH_SECTOR : last);
	}
	return ret;
}

static int font_write_default(uint32_t addr, const void *data, size_t len)
{
	int ret;

	if (len > G1_FAKE_FLASH_SECTOR || !in_range(addr, G1_FAKE_FLASH_SECTOR)) {
		return -EINVAL;
	}
	if (memcmp(data, &g1_fake_flash_image[addr], len) == 0) {
		return 0;
	}
	ret = ram_erase(NULL, addr, G1_FAKE_FLASH_SECTOR);
	if (ret == 0) {
		ret = ram_write(NULL, addr, data, len);
	}
	return ret;
}

#define STORE_KEYS     32
#define STORE_KEY_LEN  32
#define STORE_VAL_SIZE 0x200

struct store_entry {
	char key[STORE_KEY_LEN];
	uint16_t len;
	uint8_t val[STORE_VAL_SIZE];
};

static struct store_entry store[STORE_KEYS];

static struct store_entry *store_find(const char *key, bool create)
{
	for (int i = 0; i < STORE_KEYS; i++) {
		if (store[i].key[0] && strncmp(store[i].key, key, STORE_KEY_LEN) == 0) {
			return &store[i];
		}
	}
	if (!create) {
		return NULL;
	}
	for (int i = 0; i < STORE_KEYS; i++) {
		if (!store[i].key[0]) {
			strncpy(store[i].key, key, STORE_KEY_LEN - 1);
			return &store[i];
		}
	}
	return NULL;
}

static int store_write_default(const char *key, const void *val, size_t len)
{
	struct store_entry *e = store_find(key, true);

	if (e == NULL || len > STORE_VAL_SIZE) {
		return -ENOMEM;
	}
	memcpy(e->val, val, len);
	e->len = len;
	return 0;
}

static void store_del_default(const char *key)
{
	struct store_entry *e = store_find(key, false);

	if (e) {
		memset(e, 0, sizeof(*e));
	}
}

int g1_fake_store_get(const char *key, void *buf, size_t cap)
{
	struct store_entry *e = store_find(key, false);

	if (e == NULL) {
		return -ENOENT;
	}
	memcpy(buf, e->val, MIN(cap, e->len));
	return e->len;
}

static void fakes_storage_reset(bool keep_media)
{
	struct GlassesState *gs = get_dashboard_state();

	if (!keep_media) {
		memset(g1_fake_flash_image, 0xff, sizeof(g1_fake_flash_image));
		memset(store, 0, sizeof(store));
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

	RESET_FAKE(get_demo_image_source_from_flash);
	RESET_FAKE(storage_commit_request);

	RESET_FAKE(g1_fake_store_write);
	RESET_FAKE(g1_fake_store_load);
	RESET_FAKE(g1_fake_store_del);
	g1_fake_store_write_fake.custom_fake = store_write_default;
	g1_fake_store_del_fake.custom_fake = store_del_default;

	/* as init_context() sets them */
	gs->ext_flash_ok = 0;
	gs->flash_page_count = G1_FAKE_FLASH_SIZE / G1_FAKE_FLASH_SECTOR;
	gs->flash_read = g1_fake_flash_read;
	gs->flash_write = g1_fake_flash_write;
	gs->flash_erase = g1_fake_flash_erase;
	gs->store.write = g1_fake_store_write;
	gs->store.load = g1_fake_store_load;
	gs->store.del = g1_fake_store_del;
}

G1_UNIT_RESET_HOOK(fakes_storage_reset);
