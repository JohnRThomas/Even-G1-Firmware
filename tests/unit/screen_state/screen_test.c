/*
 * message_task.c with fakes for its dependencies, the worker that runs its screen handlers and
 * the helpers of screen_test.h.
 */
#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "screen_test.h"

/* the screen handlers are static */
#include "threads/message_task.c"

DEFINE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
/* send_msg_key_to_master() only */
DEFINE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_glasses_ble_status);
DEFINE_FAKE_VALUE_FUNC(uint8_t, count_unread_messages);
DEFINE_FAKE_VALUE_FUNC(uint8_t, count_messages);
DEFINE_FAKE_VOID_FUNC(clear_timeout_message, int);
DEFINE_FAKE_VOID_FUNC(mark_cur_message_read);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_msg_sync_state, int);
DEFINE_FAKE_VALUE_FUNC(bool, is_message_showing);
DEFINE_FAKE_VOID_FUNC(set_particle_pending);
DEFINE_FAKE_VOID_FUNC(cal_panel_canvas_coord, int *, int *);
DEFINE_FAKE_VOID_FUNC(teleprompter_state_reset);
DEFINE_FAKE_VALUE_FUNC(bool, is_ble_device_bound);
DEFINE_FAKE_VOID_FUNC(g1_fake_imu_set, struct imu_ctx *, uint32_t);
__weak uint8_t new_msg_signal_received;
struct g1_pkt_log g1_fake_sync_log;
uint8_t g1_fake_msg_show_state;
struct notif_msg g1_fake_msgs[MSG_TOTAL_NUM];
int g1_fake_msg_bad_index;

struct GlassesState *gs;
uint8_t *const msg_timeout_flag_ptr = &msg_timeout_flag;

static int sync_to_slave_logged(struct GlassesState *state, int cmd, void *data, size_t len) {
  uint8_t pkt[1 + 4] = {(uint8_t)cmd};
  size_t data_len = (data && len) ? MIN(len, 4) : 0; /* sync_data holds 4 bytes */

  if (data_len) {
    memcpy(&pkt[1], data, data_len);
  }
  g1_pkt_log_add(&g1_fake_sync_log, pkt, 1 + data_len);
  state->sync_ack = sync_to_slave_fake.return_val < 3000 ? 2 : 1;
  return sync_to_slave_fake.return_val;
}

__weak uint8_t get_msg_show_state(void) {
  return g1_fake_msg_show_state;
}

__weak void set_msg_show_state(uint8_t val) {
  g1_fake_msg_show_state = val;
}

__weak uint8_t g_arrMsgContent_count(void) {
  return gs->unread_msg_num;
}

__weak struct notif_msg *get_message_by_index(int idx) {
  static struct notif_msg spare_msg;

  if (idx < 0 || idx >= MSG_TOTAL_NUM) {
    g1_fake_msg_bad_index++;
    memset(&spare_msg, 0, sizeof(spare_msg));
    return &spare_msg;
  }
  return &g1_fake_msgs[idx];
}

static void imu_set_period(struct imu_ctx *ctx, uint32_t period_ms) {
  ctx->sample_period_ms = period_ms;
}

static void screen_test_reset(bool keep_media) {
  ARG_UNUSED(keep_media);
  gs = g1_state();
  msg_busy = 0;
  msg_timeout_flag = 0;
  task_status_lock = 0;

  RESET_FAKE(sync_to_slave);
  sync_to_slave_fake.custom_fake = sync_to_slave_logged;
  g1_pkt_log_clear(&g1_fake_sync_log);
  RESET_FAKE(get_glasses_ble_status);
  RESET_FAKE(count_unread_messages);
  RESET_FAKE(count_messages);
  RESET_FAKE(clear_timeout_message);
  RESET_FAKE(mark_cur_message_read);
  RESET_FAKE(get_msg_sync_state);
  RESET_FAKE(is_message_showing);
  RESET_FAKE(set_particle_pending);
  RESET_FAKE(cal_panel_canvas_coord);
  RESET_FAKE(teleprompter_state_reset);
  RESET_FAKE(is_ble_device_bound);
  RESET_FAKE(g1_fake_imu_set);
  g1_fake_imu_set_fake.custom_fake = imu_set_period;
  gs->imu.set_cb = g1_fake_imu_set;
  g1_fake_msg_show_state = 0;
  memset(g1_fake_msgs, 0, sizeof(g1_fake_msgs));
  g1_fake_msg_bad_index = 0;

  /* G1_PRINTK() lines go to ble_printk(), whose capture triggers() reads */
  BLE_DEBUG = 1;
  gs->display_mode = 10;
  head_down();
}

G1_UNIT_RESET_HOOK(screen_test_reset);

static void screen_after(void *fixture) {
  ARG_UNUSED(fixture);
  screen_stop();
}

ZTEST_SUITE(screen_state, NULL, NULL, NULL, screen_after, NULL);

typedef int (*screen_fn)(struct GlassesState *state, struct imu_ctx *ctx, const char **reason);

/* the handlers that take no IMU context */
#define RUN_WITHOUT_CTX(name)                                                                                          \
  static int run_##name(struct GlassesState *state, struct imu_ctx *ctx, const char **reason) {                        \
    ARG_UNUSED(ctx);                                                                                                   \
    return process_for_##name(state, reason);                                                                          \
  }

/* the handlers that give no exit reason */
#define RUN_WITHOUT_REASON(name)                                                                                       \
  static int run_##name(struct GlassesState *state, struct imu_ctx *ctx, const char **reason) {                        \
    ARG_UNUSED(reason);                                                                                                \
    return process_for_##name(state, ctx);                                                                             \
  }

RUN_WITHOUT_REASON(IDLE)
RUN_WITHOUT_REASON(wait_blow_head)
RUN_WITHOUT_CTX(not_disturb)
RUN_WITHOUT_CTX(onboarding)
RUN_WITHOUT_CTX(raster_height)
RUN_WITHOUT_CTX(set_imu_pitch)
RUN_WITHOUT_CTX(prompt)
RUN_WITHOUT_CTX(transcribe)
RUN_WITHOUT_CTX(even_ai_v2_show)

static int run_dispatch(struct GlassesState *state, struct imu_ctx *ctx, const char **reason) {
  ARG_UNUSED(reason);
  return screen_task_dispatch(state, ctx);
}

/* each screen's handler, as screen_task_dispatch() picks it */
static const screen_fn handlers[] = {
  [0] = run_IDLE,
  [1] = run_wait_blow_head,
  [2] = process_for_countdown,
  [4] = process_for_message_show,
  [5] = process_for_new_message_come_on,
  [6] = process_for_DASHBOARD_show,
  [7] = run_not_disturb,
  [8] = process_for_unread_msg,
  [9] = process_for_teleprompter,
  [10] = process_for_navigation,
  [11] = process_for_translate,
  [12] = process_for_quicknote,
  [14] = run_onboarding,
  [15] = run_raster_height,
  [16] = process_for_evenai,
  [17] = run_set_imu_pitch,
  [18] = run_prompt,
  [19] = run_transcribe,
  [20] = run_even_ai_v2_show,
};

static K_THREAD_STACK_DEFINE(screen_stack, 8192);
static struct k_thread screen_thread;
static bool screen_alive;
static volatile bool screen_done;
static int64_t screen_done_ms;
int screen_started;
int screen_ret;
const char *screen_reason;
int64_t screen_start_ms;

static void screen_entry(void *arg, void *unused1, void *unused2) {
  screen_fn handler = (screen_fn)arg;
  const char *reason = NULL;

  ARG_UNUSED(unused1);
  ARG_UNUSED(unused2);
  screen_ret = handler(gs, &gs->imu, &reason);
  screen_reason = reason;
  screen_done_ms = k_uptime_get();
  screen_done = true;
}

static void start_worker(screen_fn handler) {
  zassert_false(screen_alive && !screen_done, "a screen handler is already running");
  screen_done = false;
  screen_ret = RET_NOT_RETURNED;
  screen_reason = NULL;
  screen_started = gs->dashboard_mode;
  screen_start_ms = k_uptime_get();
  k_thread_create(&screen_thread, screen_stack, K_THREAD_STACK_SIZEOF(screen_stack), screen_entry, (void *)handler,
                  NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
  screen_alive = true;
}

void screen_start(void) {
  unsigned int screen = gs->dashboard_mode;

  zassert_true(screen < ARRAY_SIZE(handlers) && handlers[screen], "no handler for screen %u", screen);
  start_worker(handlers[screen]);
}

void dispatch_start(void) {
  start_worker(run_dispatch);
}

bool screen_wait(uint32_t max_ms) {
  for (uint32_t waited_ms = 0; waited_ms < max_ms && !screen_done; waited_ms++) {
    g1_clock_advance_ms(1);
  }
  return screen_done;
}

int64_t since_start(void) {
  return k_uptime_get() - screen_start_ms;
}

int64_t done_after(void) {
  return screen_done_ms - screen_start_ms;
}

void screen_stop(void) {
  if (!screen_alive) {
    return;
  }
  if (screen_done) {
    k_thread_join(&screen_thread, K_FOREVER);
  } else {
    k_thread_abort(&screen_thread);
  }
  screen_alive = false;
}

const char *triggers(void) {
  static char out[2048];
  static const char prefix[] = "trigger_screen_state_change(): ";
  static const char on_text[] = ": trigger_on_screen -> ";
  static const char ignore_text[] = ": ignore:";
  size_t used = 0;

  out[0] = '\0';
  for (int index = 0; index < g1_printk_count() && used < sizeof(out); index++) {
    const char *line = g1_printk_line(index);
    const char *on, *ignore;

    if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) {
      continue;
    }
    line += sizeof(prefix) - 1;
    on = strstr(line, on_text);
    ignore = strstr(line, ignore_text);
    if (on) {
      used += snprintf(out + used, sizeof(out) - used, "%s%.*s->%c", used ? "; " : "", (int)(on - line), line,
                       on[sizeof(on_text) - 1]);
    } else if (ignore) {
      used += snprintf(out + used, sizeof(out) - used, "%s%.*s->%c (no change)", used ? "; " : "", (int)(ignore - line),
                       line, ignore[sizeof(ignore_text) - 1]);
    }
  }
  return out;
}

const char *syncs(void) {
  static char out[1024];
  size_t used = 0;

  out[0] = '\0';
  for (int index = 0; index < g1_fake_sync_log.count && used < sizeof(out) - 32; index++) {
    char hex[64];

    g1_hex_format(g1_fake_sync_log.data[index], g1_fake_sync_log.len[index], hex, sizeof(hex));
    used += snprintf(out + used, sizeof(out) - used, "%s%s", used ? "; " : "", hex);
  }
  return out;
}

void head_up(void) {
  gs->imu.mode = 2;
}

void head_down(void) {
  gs->imu.mode = 1;
}

void showing(int screen) {
  gs->dashboard_mode = screen;
  gs->screen_on = 1;
}

void persist_task(int task, int status) {
  gs->task_status->task = task;
  gs->task_status->status = status;
  gs->sync_status.task = task;
  gs->sync_status.status = status;
}

void expect_dfu_exit(int work_mode, const char *reason) {
  gs->work_mode = work_mode;
  screen_start();
  expect_exit_within(10, RET_TASK_EXITED, reason);
  zassert_equal(gs->dashboard_mode, 0, "%s", reason);
}

void expect_wait_blow_head_exit(const char *reason) {
  start_screen_expect_running(100);
  gs->dashboard_mode = 1;
  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, reason);
}

void expect_screen_stays(uint32_t ms) {
  int screen = gs->dashboard_mode;

  start_screen_expect_running(ms);
  zassert_equal(gs->dashboard_mode, screen, "screen %d changed to %d", screen, gs->dashboard_mode);
}
