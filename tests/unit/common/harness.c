#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/fff.h>
#include <app_version.h>

#include "harness.h"
#include "ble/ble_proto.h"
#include "notify/notify.h"
#include "notify/whitelist.h"
#include "storage/storage.h"
#include "utils/common_utils.h"

DEFINE_FFF_GLOBALS;

/* main.c is never linked; its globals live here. */
struct GlassesState *GLOBAL_STATE;
int LOG_LEVEL = 2;

struct GlassesState *get_dashboard_state(void)
{
  return GLOBAL_STATE;
}

struct ble_context *get_ble_context(void)
{
  return GLOBAL_STATE ? &GLOBAL_STATE->ble_context : NULL;
}

struct GlassesState *g1_state(void)
{
  return GLOBAL_STATE;
}

struct ble_context *g1_ble(void)
{
  return get_ble_context();
}

bool g1_dashboard_notified(void)
{
  return k_sem_count_get(&GLOBAL_STATE->dashboard_position_sem) == 1;
}

#define DT_DRV_COMPAT vnd_g1_unit_fake_device

static int g1_fake_device_init(const struct device *dev)
{
  ARG_UNUSED(dev);
  return 0;
}

#define G1_FAKE_DEVICE(inst)                                                                       \
  DEVICE_DT_INST_DEFINE(inst, g1_fake_device_init, NULL, NULL, NULL, POST_KERNEL, 0, NULL);

DT_INST_FOREACH_STATUS_OKAY(G1_FAKE_DEVICE)

/*
 * Weak BLE_DEBUG and ble_printk(): ble_proto.c's replace them when the suite links it. This
 * ble_printk() keeps the lines for g1_printk_count() and g1_printk_line().
 */
#define G1_PRINTK_LINES     256
#define G1_PRINTK_LINE_SIZE 200 /* DEBUG_MSG_SIZE */

__weak int BLE_DEBUG;
static char printk_lines[G1_PRINTK_LINES][G1_PRINTK_LINE_SIZE];
static int printk_count;

__weak void ble_printk(const char *fmt, ...)
{
  va_list args;

  if (printk_count == G1_PRINTK_LINES) {
    return;
  }
  va_start(args, fmt);
  vsnprintf(printk_lines[printk_count++], G1_PRINTK_LINE_SIZE, fmt, args);
  va_end(args);
}

int g1_printk_count(void)
{
  return printk_count;
}

const char *g1_printk_line(int index)
{
  return index >= 0 && index < printk_count ? printk_lines[index] : NULL;
}

/* Weak: NULL unless the suite links the defining file. */
void crc32_init(void);
int file_init(void);
int dequeue_file(void *out);
void ble_proto_test_reset(void);

#pragma weak ble_proto_test_reset
#pragma weak BLE_DEBUG
#pragma weak ble_printk
#pragma weak app_list_init
#pragma weak load_app_white_list_from_flash
#pragma weak setting_msgq
#pragma weak quicknote_msgq
#pragma weak getAppLanguageInfofromFlash
#pragma weak getDashboardStartUpModeInfofromFlash
#pragma weak CURRENT_LANGUAGE
#pragma weak flash_ops_sem
#pragma weak LAST_FILE_CRC
#pragma weak sys_settings_dirty
#pragma weak crc32_init
#pragma weak file_init
#pragma weak dequeue_file
#pragma weak SERIAL_NUMBER
#pragma weak glasses_name
#pragma weak init_boot_seconds
#pragma weak set_wall_clock_ms

/* routes like ble_proto.c's: ble_printk() when BLE_DEBUG is set, otherwise printk() */
__weak void g1_printk(const char *fmt, ...)
{
  char buf[200];
  va_list ap;

  va_start(ap, fmt);
  if (&BLE_DEBUG && BLE_DEBUG && ble_printk) {
    vsnprintf(buf, sizeof(buf), fmt, ap);
    ble_printk("%s", buf);
  } else {
    vprintk(fmt, ap);
  }
  va_end(ap);
}

extern struct k_msgq setting_msgq;
extern struct k_msgq quicknote_msgq;

void g1_clock_advance_ms(uint32_t ms)
{
  k_sleep(K_TIMEOUT_ABS_MS(k_uptime_get() + ms));
}

int64_t g1_clock_uptime_ms(void)
{
  return k_uptime_get();
}

void g1_clock_set_date(uint32_t unix_s)
{
  GLOBAL_STATE->date_ts = unix_s;
}

static K_THREAD_STACK_ARRAY_DEFINE(thread_stacks, G1_THREADS_MAX, 16384);
static struct k_thread threads[G1_THREADS_MAX];
static bool thread_started[G1_THREADS_MAX];

k_tid_t g1_thread_start(k_thread_entry_t entry, void *arg, int prio)
{
  for (int index = 0; index < G1_THREADS_MAX; index++) {
    if (!thread_started[index]) {
      thread_started[index] = true;
      return k_thread_create(&threads[index], thread_stacks[index],
                 K_THREAD_STACK_SIZEOF(thread_stacks[index]), entry,
                 arg, NULL, NULL, prio, 0, K_NO_WAIT);
    }
  }
  zassert_unreachable("more than %d harness threads in one test", G1_THREADS_MAX);
  return NULL;
}

/* Fixed address: firmware keeps pointers into it. */
static struct GlassesState state_storage;

#define G1_BLOCKS(BLOCK)                                                                           \
  BLOCK(dashboard_ts, 0x75)                                                                  \
  BLOCK(countdown_ts, 7)                                                                     \
  BLOCK(task_status, 5)                                                                      \
  BLOCK(teleprompter, 0x217)                                                                 \
  BLOCK(nav, 0xf5)                                                                           \
  BLOCK(translate_src, 0x1e8)                                                                \
  BLOCK(translate_dst, 0x1e8)                                                                \
  BLOCK(dash_buf, 0x1e9)                                                                     \
  BLOCK(evenai_buf, 0x210)                                                                   \
  BLOCK(quicknote, 0x50f)                                                                    \
  BLOCK(quicknote_show_info, 0x152)                                                          \
  BLOCK(esb_up_snapshot, 0x201)                                                              \
  BLOCK(esb_down_snapshot, 0x201)                                                            \
  BLOCK(user_sched_info, 0x16a)                                                              \
  BLOCK(schedule_notif, 0xaf)                                                                \
  BLOCK(bitmap_info, 0xb)                                                                    \
  BLOCK(onboarding, 0x21)                                                                    \
  BLOCK(raster_height_cfg, 1)                                                                \
  BLOCK(horiz_pitch_cfg, 2)                                                                  \
  BLOCK(ts_info, 10)                                                                         \
  BLOCK(transcribe_buf, 0x208)

static void *block_alloc(size_t size)
{
  void *block = malloc(size);

  __ASSERT(block != NULL, "harness: out of heap (CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE)");
  memset(block, 0, size);
  return block;
}

static void state_free(struct GlassesState *state)
{
#define G1_FREE(field, size) free(state->field);
  G1_BLOCKS(G1_FREE)
#undef G1_FREE
  free(state->ble_context.tx_buf);
  free(state->ble_context.small_buf);
}

static void ble_context_setup(struct ble_context *ctx)
{
  ctx->write = g1_frame_capture;
  k_sem_init(&ctx->rx_sem, 0, 1);
  k_sem_init(&ctx->tx_sem, 0, 2);
  ctx->small_buf = block_alloc(BLE_SMALL_BUF_SIZE);
  ctx->tx_buf = block_alloc(BLE_TX_BUF_SIZE);
}

/* as role_init(): the VERSION file's version and the BIT(2) flag on the side's own record */
static void set_role(struct GlassesState *state, int role)
{
  static const struct fw_ver version = {APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL};
  bool master = role == G1_ROLE_MASTER;

  state->is_master = role;
  state->master_fw_ver = master ? version : (struct fw_ver){0};
  state->fw_ver = master ? (struct fw_ver){0} : version;
  WRITE_BIT(state->sync.flags, 2, master);
  WRITE_BIT(state->esb_pkt.flags, 2, !master);
}

static void state_build(void)
{
  struct GlassesState *state = &state_storage;

  if (GLOBAL_STATE == state) {
    state_free(state);
  }
  /* init_context() minus hardware and flash */
  memset(state, 0, sizeof(*state));
  memset(state->master_mac, 0xff, sizeof(state->master_mac));
  memset(state->slave_mac, 0xff, sizeof(state->slave_mac));
  GLOBAL_STATE = state;
  set_role(state, G1_ROLE_MASTER);

  k_sem_init(&state->ipc_sem, 0, 1);
  k_sem_init(&state->sem_1, 0, 10);
  k_sem_init(&state->sem_2, 0, 1);
  k_sem_init(&state->screen_on_sem, 0, 1);
  k_sem_init(&state->imu_sem, 0, 1);
  k_sem_init(&state->main_sem, 0, 1);
  k_sem_init(&state->dashboard_position_sem, 0, 1);
  k_sem_init(&state->sem_7, 0, 1);
  k_sem_init(&state->touch_sem, 0, 1);

  /* change_work_mode(0) from 0xff */
  state->module_mask = 0x0dfb;

#define G1_ALLOC(field, size) state->field = block_alloc(size);
  G1_BLOCKS(G1_ALLOC)
#undef G1_ALLOC
  state->date_ts = G1_RESET_DATE;

  memset(&state->bonded_addr, 0xff, sizeof(state->bonded_addr));

  state->spi.is_initialized = 0;
  state->spi.pins[0] = 4;
  state->spi.pins[1] = 8;
  state->spi.pins[2] = 9;
  state->spi.pins[3] = 10;
  state->spi.ss = 11;
  state->spi.speed_mhz = 0x20;

  state->master_fw_ver.major = APP_VERSION_MAJOR;
  state->master_fw_ver.minor = APP_VERSION_MINOR;
  state->master_fw_ver.patch = APP_PATCHLEVEL;
  state->sync.flags |= BIT(2);

  ble_context_setup(&state->ble_context);

  state->user_data_reset_pending = 0;
  state->panel.brightness_max = state->panel.lum_gear;
  state->mic_nocompress = 0;
}

void g1_unit_set_role(int role)
{
  struct GlassesState *state = &state_storage;
  zassert_true(role == G1_ROLE_MASTER || role == G1_ROLE_SLAVE, "role %d", role);
  memset(&state->master_fw_ver, 0, sizeof(state->master_fw_ver));
  memset(&state->fw_ver, 0, sizeof(state->fw_ver));
  state->sync.flags &= ~BIT(2);
  state->esb_pkt.flags &= ~BIT(2);
  state->is_master = role;
  if (role == G1_ROLE_MASTER) {
    state->master_fw_ver.major = APP_VERSION_MAJOR;
    state->master_fw_ver.minor = APP_VERSION_MINOR;
    state->master_fw_ver.patch = APP_PATCHLEVEL;
    state->sync.flags |= BIT(2);
  } else {
    state->fw_ver.major = APP_VERSION_MAJOR;
    state->fw_ver.minor = APP_VERSION_MINOR;
    state->fw_ver.patch = APP_PATCHLEVEL;
    state->esb_pkt.flags |= BIT(2);
  }
}

#define G1_RESET_HOOKS_MAX 32

static g1_unit_reset_fn reset_hooks[G1_RESET_HOOKS_MAX];
static int reset_hook_count;

void g1_unit_register_reset(g1_unit_reset_fn hook)
{
  __ASSERT(reset_hook_count < G1_RESET_HOOKS_MAX, "harness: too many reset hooks");
  reset_hooks[reset_hook_count++] = hook;
}

static void run_hooks(bool keep_media)
{
  for (int index = 0; index < reset_hook_count; index++) {
    reset_hooks[index](keep_media);
  }
}

/* no public prototype */
void __printk_hook_install(int (*fn)(int character));
void *__printk_get_hook(void);

static int quiet_char_out(int character)
{
  return character;
}

static bool snapshot_taken;
static char serial_number_init[32];
static char glasses_name_init[32];
static bool file_queue_ready;

static void firmware_reset(void)
{
  struct GlassesState *state = GLOBAL_STATE;

  if (!snapshot_taken) {
    if (SERIAL_NUMBER) {
      memcpy(serial_number_init, SERIAL_NUMBER, sizeof(serial_number_init));
    }
    if (glasses_name) {
      memcpy(glasses_name_init, glasses_name, sizeof(glasses_name_init));
    }
    snapshot_taken = true;
  }
  if (SERIAL_NUMBER) {
    memcpy(SERIAL_NUMBER, serial_number_init, sizeof(serial_number_init));
  }
  if (glasses_name) {
    memcpy(glasses_name, glasses_name_init, sizeof(glasses_name_init));
  }

  if (ble_proto_test_reset) {
    ble_proto_test_reset();
  }
  BLE_DEBUG = 0;

  if (&flash_ops_sem) {
    k_sem_init(&flash_ops_sem, 0, 20);
  }
  if (crc32_init) {
    crc32_init();
  }
  if (&LAST_FILE_CRC) {
    LAST_FILE_CRC = 0;
  }
  if (&sys_settings_dirty) {
    sys_settings_dirty = 0;
  }
  if (file_init && dequeue_file) {
    uint8_t msg[256];

    if (!file_queue_ready) {
      file_init();
      file_queue_ready = true;
    }
    while (dequeue_file(msg) == 0) {
    }
  }

  /* Purged directly: clearSettingStorageQueue() is unreliable. */
  if (&setting_msgq) {
    k_msgq_purge(&setting_msgq);
  }
  if (&quicknote_msgq) {
    k_msgq_purge(&quicknote_msgq);
  }

  if (&CURRENT_LANGUAGE) {
    memset((void *)&CURRENT_LANGUAGE, 0, sizeof(CURRENT_LANGUAGE));
  }
  if (state->flash_read) {
    if (getDashboardStartUpModeInfofromFlash) {
      getDashboardStartUpModeInfofromFlash();
    }
    if (getAppLanguageInfofromFlash) {
      getAppLanguageInfofromFlash();
    }
    if (app_list_init) {
      app_list_init();
    } else if (load_app_white_list_from_flash) {
      load_app_white_list_from_flash();
    }
  }
}

static void clock_reset(void)
{
  int64_t now = k_uptime_get();

  /* whole second, so s = ms / 1000 is exact */
  if (now % 1000) {
    k_sleep(K_TIMEOUT_ABS_MS(now - now % 1000 + 1000));
  }
  if (init_boot_seconds) {
    init_boot_seconds();
  }
  if (set_wall_clock_ms) {
    set_wall_clock_ms(0);
  }
  GLOBAL_STATE->date_ts = G1_RESET_DATE;
}

static void reset(bool keep_media)
{
  void *hook = __printk_get_hook();

  __printk_hook_install(quiet_char_out);

  state_build();
  run_hooks(keep_media);
  firmware_reset();
  run_hooks(true);
  g1_frames_clear();
  printk_count = 0;
  g1_write_ret = 0;
  LOG_LEVEL = 2;
  FFF_RESET_HISTORY();

  __printk_hook_install(hook);
  clock_reset();
}

void g1_unit_reset(void)
{
  reset(false);
}

void g1_unit_reboot(void)
{
  reset(true);
}

static void g1_unit_before_each(const struct ztest_unit_test *test, void *fixture)
{
  ARG_UNUSED(test);
  ARG_UNUSED(fixture);
  g1_unit_reset();
}

static void g1_unit_after_each(const struct ztest_unit_test *test, void *fixture)
{
  ARG_UNUSED(test);
  ARG_UNUSED(fixture);
  for (int index = 0; index < G1_THREADS_MAX; index++) {
    if (thread_started[index]) {
      k_thread_abort(&threads[index]);
      thread_started[index] = false;
    }
  }
}

ZTEST_RULE(g1_unit_harness, g1_unit_before_each, g1_unit_after_each);
