/* task_status.c (task record, screen id, lock) and system_state.c (screen, IMU, suspend). */
#include "screen_test.h"

/* GlassesState->panel callbacks: each call logs its op ('S' suspend, 'R' resume), state and time */
FAKE_VALUE_FUNC(int, g1_fake_panel_suspend, struct jbd_panel_context *);
FAKE_VALUE_FUNC(int, g1_fake_panel_resume, struct jbd_panel_context *);

static struct {
  char op;
  uint8_t suspend_state;
  int64_t uptime_ms;
} panel_calls[8];
static unsigned int panel_call_count;

static int panel_call(char op)
{
  if (panel_call_count < ARRAY_SIZE(panel_calls)) {
    panel_calls[panel_call_count].op = op;
    panel_calls[panel_call_count].suspend_state = gs->suspend_state;
    panel_calls[panel_call_count].uptime_ms = k_uptime_get();
    panel_call_count++;
  }
  return 0;
}

static int panel_suspend_logged(struct jbd_panel_context *panel)
{
  ARG_UNUSED(panel);
  return panel_call('S');
}

static int panel_resume_logged(struct jbd_panel_context *panel)
{
  ARG_UNUSED(panel);
  return panel_call('R');
}

static void panel_fakes_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);
  RESET_FAKE(g1_fake_panel_suspend);
  RESET_FAKE(g1_fake_panel_resume);
  g1_fake_panel_suspend_fake.custom_fake = panel_suspend_logged;
  g1_fake_panel_resume_fake.custom_fake = panel_resume_logged;
  g1_state()->panel.suspend_cb = g1_fake_panel_suspend;
  g1_state()->panel.resume_cb = g1_fake_panel_resume;
  panel_call_count = 0;
}

G1_UNIT_RESET_HOOK(panel_fakes_reset);

#define expect_record(task_id, task_state)                                                         \
  do {                                                                                       \
    zassert_equal(gs->task_status->task, (task_id), "task %u", gs->task_status->task); \
    zassert_equal(gs->task_status->status, (task_state), "status %u",                  \
            gs->task_status->status);                                            \
  } while (0)

ZTEST(screen_state, test_persist_task_active_when_running_or_paused)
{
  for (int status = 0; status <= 3; status++) {
    persist_task(9, status);
    zassert_equal(is_persist_task_active(gs, 0), status >= 2, "status %d", status);
  }
}

ZTEST(screen_state, test_task_running_means_screen_above_1)
{
  static const uint8_t screens[] = {0, 1, 2, 20};

  for (size_t index = 0; index < ARRAY_SIZE(screens); index++) {
    gs->dashboard_mode = screens[index];
    zassert_equal(is_task_running(gs), screens[index] > 1, "screen %d", screens[index]);
  }
}

ZTEST(screen_state, test_persist_update_from_idle_sets_screen_and_record)
{
  gs->dashboard_mode = 0;

  zassert_equal(update_persist_task_status(gs, 10, 2), 2);

  zassert_equal(gs->dashboard_mode, 10);
  expect_record(10, 2);
  zassert_equal(display_close_screen_fake.call_count, 0, "screen 0 has nothing to close");
}

ZTEST(screen_state, test_persist_update_from_wait_blow_head_sets_screen)
{
  gs->dashboard_mode = 1;

  update_persist_task_status(gs, 9, 2);

  zassert_equal(gs->dashboard_mode, 9);
  expect_record(9, 2);
  zassert_equal(display_close_screen_fake.call_count, 0);
}

/* the update returns the status asked for and changes nothing */
ZTEST(screen_state, test_persist_update_ignored_or_refused)
{
  static const struct {
    const char *why;
    uint8_t screen;
    uint8_t record_task;
    uint8_t record_status;
    uint8_t task;
    uint8_t status;
  } rows[] = {
    {"same task and status", 9, 9, 2, 9, 2},
    {"pause of a stopped task", 6, 2, 1, 2, 3},
    {"running task over another screen", 10, 10, 2, 9, 2},
    {"over the dashboard without exit request", 6, 0, 1, 9, 2},
  };

  for (size_t index = 0; index < ARRAY_SIZE(rows); index++) {
    const char *why = rows[index].why;

    gs->dashboard_mode = rows[index].screen;
    persist_task(rows[index].record_task, rows[index].record_status);

    zassert_equal(update_persist_task_status(gs, rows[index].task, rows[index].status),
            rows[index].status, "%s", why);

    zassert_equal(display_close_screen_fake.call_count, 0, "%s", why);
    zassert_equal(gs->dashboard_mode, rows[index].screen, "%s", why);
    zassert_equal(gs->task_status->task, rows[index].record_task, "%s", why);
    zassert_equal(gs->task_status->status, rows[index].record_status, "%s", why);
    zassert_equal(task_status_lock, 0, "%s", why);
  }
}

ZTEST(screen_state, test_persist_update_running_overlay_task_replaces_screen)
{
  static const uint8_t tasks[] = {4, 6, 7, 16, 20};

  for (size_t index = 0; index < ARRAY_SIZE(tasks); index++) {
    RESET_FAKE(display_close_screen);
    gs->dashboard_mode = 10;
    persist_task(10, 2);

    update_persist_task_status(gs, tasks[index], 2);

    zassert_equal(display_close_screen_fake.call_count, 1, "task %d", tasks[index]);
    zassert_equal(display_close_screen_fake.arg0_val, 10, "task %d", tasks[index]);
    zassert_equal(gs->dashboard_mode, tasks[index], "task %d", tasks[index]);
    expect_record(tasks[index], 2);
  }
}

ZTEST(screen_state, test_persist_update_pause_over_screen_switches_to_task)
{
  gs->dashboard_mode = 10;
  persist_task(2, 2);

  zassert_equal(update_persist_task_status(gs, 2, 3), 3);

  zassert_equal(display_close_screen_fake.arg0_val, 10);
  zassert_equal(gs->dashboard_mode, 2);
  expect_record(2, 3);
}

ZTEST(screen_state, test_persist_update_during_dashboard_exit_records_only)
{
  gs->dashboard_mode = 6;
  gs->dashboard_ts->exit_pending = 1;
  persist_task(0, 1);

  update_persist_task_status(gs, 9, 2);

  zassert_equal(gs->dashboard_mode, 6);
  zassert_equal(display_close_screen_fake.call_count, 0);
  expect_record(9, 2);
}

/* even AI turns auto brightness off; going idle restores the setting */
ZTEST(screen_state, test_persist_update_even_ai_suspends_auto_brightness_until_idle)
{
  for (uint8_t setting = 0; setting <= 1; setting++) {
    /* as after a reset: the record task 16 stopped would make the update a no-op */
    persist_task(0, 0);
    gs->dashboard_mode = 0;
    gs->opt3007_context.auto_brightness = setting;

    update_persist_task_status(gs, 16, 2);
    zassert_equal(gs->opt3007_context.auto_brightness, 0, "setting %d", setting);

    update_persist_task_status_to_idle(gs);
    zassert_equal(gs->opt3007_context.auto_brightness, setting, "setting %d", setting);
  }
}

ZTEST(screen_state, test_persist_update_running_resets_even_ai_v2_state)
{
  gs->dashboard_mode = 0;
  gs->evenai_buf->state = 2;

  update_persist_task_status(gs, 10, 2);
  zassert_equal(gs->evenai_buf->state, 0);

  gs->evenai_buf->state = 1;
  update_persist_task_status(gs, 11, 2);
  zassert_equal(gs->evenai_buf->state, 1, "state 1 is kept");
}

ZTEST(screen_state, test_temp_update_from_idle_sets_screen_and_clears_record)
{
  gs->dashboard_mode = 0;
  persist_task(9, 1);

  zassert_equal(update_temp_task_status(gs, 6, 2), 2);

  zassert_equal(gs->dashboard_mode, 6);
  expect_record(0, 1);
  zassert_equal(display_close_screen_fake.call_count, 0);
}

ZTEST(screen_state, test_temp_update_message_over_screen_closes_old_screen)
{
  gs->dashboard_mode = 2;
  persist_task(2, 2);

  update_temp_task_status(gs, 4, 2);

  zassert_equal(display_close_screen_fake.arg0_val, 2);
  zassert_equal(gs->dashboard_mode, 4);
  expect_record(0, 1);
}

ZTEST(screen_state, test_temp_update_other_task_over_screen_is_refused)
{
  gs->dashboard_mode = 5;
  persist_task(0, 1);

  zassert_equal(update_temp_task_status(gs, 8, 2), 2);

  zassert_equal(gs->dashboard_mode, 5);
  zassert_equal(display_close_screen_fake.call_count, 0);
  zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_temp_update_current_screen_is_ignored)
{
  gs->dashboard_mode = 6;
  persist_task(9, 2);

  update_temp_task_status(gs, 6, 2);

  zassert_equal(display_close_screen_fake.call_count, 0);
  expect_record(9, 2);
}

ZTEST(screen_state, test_temp_update_stop_over_screen_switches)
{
  gs->dashboard_mode = 9;
  persist_task(9, 2);

  update_temp_task_status(gs, 5, 1);

  zassert_equal(display_close_screen_fake.arg0_val, 9);
  zassert_equal(gs->dashboard_mode, 5);
  expect_record(0, 1);
}

ZTEST(screen_state, test_to_idle_closes_screen_and_stops_record)
{
  gs->dashboard_mode = 10;
  persist_task(10, 2);

  zassert_equal(update_persist_task_status_to_idle(gs), 0);

  zassert_equal(display_close_screen_fake.arg0_val, 10);
  zassert_equal(gs->dashboard_mode, 0);
  expect_record(10, 1);
}

ZTEST(screen_state, test_to_idle_when_idle_does_nothing)
{
  gs->dashboard_mode = 0;
  persist_task(0, 1);

  update_persist_task_status_to_idle(gs);

  zassert_equal(display_close_screen_fake.call_count, 0);
  expect_record(0, 1);
}

ZTEST(screen_state, test_to_idle_from_wait_blow_head_closes_nothing)
{
  gs->dashboard_mode = 1;
  persist_task(1, 1);

  update_persist_task_status_to_idle(gs);

  zassert_equal(display_close_screen_fake.call_count, 0);
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_to_wait_blow_head_closes_screen_and_records_1)
{
  gs->dashboard_mode = 12;
  persist_task(12, 2);

  zassert_equal(update_persist_task_status_to_wait_blow_head(gs), 0);

  zassert_equal(display_close_screen_fake.arg0_val, 12);
  zassert_equal(gs->dashboard_mode, 1);
  expect_record(1, 1);
}

/* 9 teleprompter, 11 translate, 12 quicknote, 16 / 20 even AI, 19 transcribe */
ZTEST(screen_state, test_task_uses_mic_for_voice_tasks_only)
{
  for (int task = 0; task <= 25; task++) {
    bool uses_mic = task == 9 || task == 11 || task == 12 || task == 16 || task == 19 ||
        task == 20;

    zassert_equal(task_uses_mic(task), uses_mic, "task %d", task);
  }
}

ZTEST(screen_state, test_is_idle_needs_stopped_record_screen_0_and_suspend)
{
  persist_task(0, 1);
  gs->dashboard_mode = 0;
  gs->suspend_state = 1;
  zassert_true(is_idle());

  gs->suspend_state = 0;
  zassert_false(is_idle(), "not suspended");
  gs->suspend_state = 1;

  persist_task(9, 1);
  zassert_false(is_idle(), "task id kept");
  persist_task(0, 2);
  zassert_false(is_idle(), "task running");
  persist_task(0, 1);

  gs->dashboard_mode = 1;
  zassert_false(is_idle(), "screen 1");
}

static struct k_timer unlock_timer;

static void unlock_expiry(struct k_timer *timer)
{
  ARG_UNUSED(timer);
  task_status_lock = 0;
}

/* task_status_lock is held until a timer releases it after ms; returns the uptime at the start */
static int64_t hold_task_status_lock(uint32_t ms)
{
  k_timer_init(&unlock_timer, unlock_expiry, NULL);
  task_status_lock = 1;
  k_timer_start(&unlock_timer, K_MSEC(ms), K_NO_WAIT);
  return k_uptime_get();
}

ZTEST(screen_state, test_task_status_lock_makes_update_wait)
{
  int64_t start_ms;

  gs->dashboard_mode = 0;
  start_ms = hold_task_status_lock(20);

  update_persist_task_status(gs, 10, 2);

  expect_elapsed_ms(start_ms, 20, INT64_MAX);
  zassert_equal(gs->dashboard_mode, 10);
  zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_task_status_lock_makes_readers_wait)
{
  int64_t start_ms;

  persist_task(9, 2);
  start_ms = hold_task_status_lock(15);

  zassert_equal(is_persist_task_active(gs, 0), 1);
  expect_elapsed_ms(start_ms, 15, INT64_MAX);
}

ZTEST(screen_state, test_task_status_lock_released_on_every_path)
{
  gs->dashboard_mode = 9;
  persist_task(9, 2);
  update_persist_task_status(gs, 9, 2); /* unchanged */
  zassert_equal(task_status_lock, 0);
  update_persist_task_status(gs, 10, 2); /* refused */
  zassert_equal(task_status_lock, 0);
  update_temp_task_status(gs, 9, 2); /* current screen */
  zassert_equal(task_status_lock, 0);
  update_temp_task_status(gs, 8, 2); /* refused */
  zassert_equal(task_status_lock, 0);
  update_persist_task_status_to_idle(gs);
  zassert_equal(task_status_lock, 0);
  update_persist_task_status_to_idle(gs); /* already idle */
  zassert_equal(task_status_lock, 0);
  update_persist_task_status_to_wait_blow_head(gs);
  zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_screen_on_sets_flag_and_gives_semaphore)
{
  zassert_equal(trigger_screen_state_change("t", gs, 1), 1);
  zassert_equal(gs->screen_on, 1);
  zassert_equal(k_sem_count_get(&gs->screen_on_sem), 1);

  zassert_equal(trigger_screen_state_change("t", gs, 0), 0);
  zassert_equal(gs->screen_on, 0);
}

ZTEST(screen_state, test_screen_on_repeated_is_ignored_without_counting)
{
  trigger_screen_state_change("a", gs, 1);
  k_sem_take(&gs->screen_on_sem, K_NO_WAIT);

  zassert_equal(trigger_screen_state_change("b", gs, 1), -1);
  zassert_equal(k_sem_count_get(&gs->screen_on_sem), 0);

  zassert_equal(trigger_screen_state_change("c", gs, 0), 0);
  zassert_equal(gs->screen_on, 0);
  zassert_equal(trigger_screen_state_change("d", gs, 0), -1);
}

ZTEST(screen_state, test_imu_on_in_normal_mode_wakes_imu_thread)
{
  zassert_equal(set_imu_on_state(NULL, gs, 1), 0);
  zassert_equal(gs->imu.on, 1);
  zassert_equal(gs->imu_event, 1);
  zassert_equal(k_sem_count_get(&gs->imu_sem), 1);

  zassert_equal(set_imu_on_state(NULL, gs, 1), -1);
}

ZTEST(screen_state, test_imu_on_while_imu_busy_or_other_mode_does_not_wake)
{
  gs->imu_busy = 1;
  set_imu_on_state(NULL, gs, 1);
  zassert_equal(gs->imu.on, 1);
  zassert_equal(gs->imu_event, 0);
  zassert_equal(k_sem_count_get(&gs->imu_sem), 0);

  set_imu_on_state(NULL, gs, 0);
  gs->imu_busy = 0;
  gs->display_mode = 12;
  set_imu_on_state(NULL, gs, 1);
  zassert_equal(gs->imu_event, 0);
}

ZTEST(screen_state, test_imu_state_change_switches_imu_and_screen)
{
  zassert_equal(trigger_imu_state_change(NULL, gs, true), 0);
  zassert_equal(gs->imu.on, 1);
  zassert_equal(gs->screen_on, 1);

  zassert_equal(trigger_imu_state_change(NULL, gs, false), 0);
  zassert_equal(gs->imu.on, 0);
  zassert_equal(gs->screen_on, 0);

  zassert_equal(g1_printk_count(), 2);
  zassert_not_null(strstr(g1_printk_line(0), "imu-on: trigger_on_screen -> 1"));
  zassert_not_null(strstr(g1_printk_line(1), "imu-off: trigger_on_screen -> 0"));
}

ZTEST(screen_state, test_imu_state_change_repeated_on_is_not_counted)
{
  trigger_imu_state_change(NULL, gs, true);
  gs->imu_event = 0;
  k_sem_take(&gs->imu_sem, K_NO_WAIT);

  trigger_imu_state_change(NULL, gs, true);
  zassert_equal(gs->imu_event, 0);
  zassert_equal(k_sem_count_get(&gs->imu_sem), 0);

  trigger_imu_state_change(NULL, gs, false);
  zassert_equal(gs->imu.on, 0);
  zassert_equal(gs->screen_on, 0);
}

ZTEST(screen_state, test_suspend_waits_300ms_then_suspends_panel)
{
  int64_t start_ms = k_uptime_get();

  gs->suspend_state = 0;

  zassert_equal(global_system_suspend(gs), 0);

  zassert_equal(gs->suspend_state, 1);
  zassert_equal(panel_call_count, 1);
  zassert_equal(panel_calls[0].op, 'S');
  zassert_equal(panel_calls[0].suspend_state, 2);
  zassert_true(panel_calls[0].uptime_ms - start_ms >= 300, "after %d ms",
         (int)(panel_calls[0].uptime_ms - start_ms));
  zassert_equal(g1_fake_panel_resume_fake.call_count, 0);
}

/* bit 2 is a flag */
ZTEST(screen_state, test_suspend_from_state_4)
{
  gs->suspend_state = 4;

  global_system_suspend(gs);

  zassert_equal(gs->suspend_state, 1);
  zassert_equal(g1_fake_panel_suspend_fake.call_count, 1);
}

ZTEST(screen_state, test_suspend_when_not_running_does_nothing)
{
  for (uint8_t state = 1; state <= 3; state++) {
    gs->suspend_state = state;
    global_system_suspend(gs);
    zassert_equal(gs->suspend_state, state);
  }
  zassert_equal(g1_fake_panel_suspend_fake.call_count, 0);
}

ZTEST(screen_state, test_resume_resumes_panel_then_runs)
{
  int64_t start_ms = k_uptime_get();

  gs->suspend_state = 1;

  zassert_equal(global_system_resume(gs), 0);

  zassert_equal(gs->suspend_state, 0);
  zassert_equal(panel_call_count, 1);
  zassert_equal(panel_calls[0].op, 'R');
  zassert_equal(panel_calls[0].suspend_state, 3);
  zassert_equal(panel_calls[0].uptime_ms, start_ms);
}

ZTEST(screen_state, test_resume_from_state_5)
{
  gs->suspend_state = 5;

  global_system_resume(gs);

  zassert_equal(gs->suspend_state, 0);
  zassert_equal(g1_fake_panel_resume_fake.call_count, 1);
}

ZTEST(screen_state, test_resume_when_not_suspended_does_nothing)
{
  static const uint8_t states[] = {0, 2, 3, 4};

  for (size_t index = 0; index < ARRAY_SIZE(states); index++) {
    gs->suspend_state = states[index];
    global_system_resume(gs);
    zassert_equal(gs->suspend_state, states[index]);
  }
  zassert_equal(g1_fake_panel_resume_fake.call_count, 0);
}
