/* The wait for head-down (1), countdown (2) and dashboard (6) screens. */
#include "screen_test.h"

ZTEST(screen_state, test_wait_blow_head_turns_screen_off_and_waits_while_head_up)
{
  showing(1);
  head_up();

  expect_screen_stays(1000);

  expect_str(triggers(), "WAIT_BLOW_HEAD->0");
  zassert_equal(gs->screen_on, 0);
}

ZTEST(screen_state, test_wait_blow_head_head_down_goes_idle)
{
  showing(1);
  head_up();

  start_screen_expect_running(200);
  head_down();

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_wait_blow_head_double_tap_dashboard_setting_goes_idle)
{
  showing(1);
  head_up();
  gs->double_tap_customize = 4;

  screen_start();

  expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_wait_blow_head_even_ai_start_wakes_screen)
{
  showing(1);
  head_up();

  start_screen_expect_running(100);
  persist_task(16, 2);

  expect_exit_within(60, 0, NULL);
  zassert_equal(gs->dashboard_mode, 16);
  expect_str(triggers(), "WAIT_BLOW_HEAD->0; BLE:wakeup:persist_task->1");
  zassert_equal(gs->screen_on, 1);
}

static void countdown_setup(void)
{
  showing(2);
  persist_task(2, 2);
  gs->countdown_ts->enabled = 1;
  gs->countdown_ts->seconds = 60;
}

ZTEST(screen_state, test_countdown_stays_while_running)
{
  countdown_setup();
  expect_screen_stays(2000);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_countdown_disabled_by_app_closes_to_idle)
{
  countdown_setup();

  start_screen_expect_running(200);
  gs->countdown_ts->enabled = 0;

  expect_task_closed(SELF_EXIT_MS, "countdown:trigger-off-imm");
  zassert_equal(gs->task_status->status, 1);
  zassert_equal(display_close_screen_fake.arg0_val, 2);
  zassert_true(done_after() >= 200 + 500, "exit after %d ms", (int)done_after());
}

ZTEST(screen_state, test_countdown_slave_disabled_closes_without_delay)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  countdown_setup();
  gs->countdown_ts->enabled = 0;

  screen_start();

  expect_exit_within(10, RET_TASK_EXITED, "countdown:trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_countdown_disabled_with_head_up_waits_for_head_down)
{
  countdown_setup();
  gs->countdown_ts->enabled = 0;
  head_up();

  screen_start();

  expect_exit_within(600, RET_TASK_EXITED, "countdown:trigger-off-imm");
  expect_str(syncs(), "08");
  zassert_equal(gs->dashboard_mode, 1);
  zassert_equal(gs->task_status->task, 1);
  zassert_equal(gs->task_status->status, 1);
}

ZTEST(screen_state, test_countdown_closes_in_silent_mode)
{
  countdown_setup();

  start_screen_expect_running(100);
  gs->display_mode = 12;

  expect_exit_within(SELF_EXIT_MS, RET_TASK_EXITED, "countdown:trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_countdown_dfu_closes_to_idle)
{
  countdown_setup();
  expect_dfu_exit(1, "countdown:imu-trigger-off-imm");
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_countdown_time_over_stops_task)
{
  countdown_setup();

  start_screen_expect_running(100);
  gs->countdown_ts->seconds = 0;

  expect_exit_within(60, RET_TASK_EXITED, "countdown:ts is over");
  zassert_equal(gs->countdown_ts->enabled, 0);
  zassert_equal(gs->task_status->task, 2);
  zassert_equal(gs->task_status->status, 1);
  zassert_equal(display_close_screen_fake.call_count, 1);
  zassert_equal(display_close_screen_fake.arg0_val, 2);
}

ZTEST(screen_state, test_countdown_head_up_opens_dashboard)
{
  countdown_setup();

  start_screen_expect_running(100);
  head_up();

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 6);
  zassert_equal(gs->dashboard_ts->dashboard_on, 1);
  zassert_equal(display_close_screen_fake.arg0_val, 2);
}

ZTEST(screen_state, test_countdown_head_up_with_unread_opens_messages)
{
  countdown_setup();
  gs->unread_msg_num = 2;
  count_unread_messages_fake.return_val = 2;
  head_up();

  screen_start();

  expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 4);
  zassert_equal(*msg_timeout_flag_ptr, 1);
  zassert_equal(gs->dashboard_ts->dashboard_on, 0);
}

ZTEST(screen_state, test_countdown_wait_blow_head_request_ends_screen)
{
  countdown_setup();

  start_screen_expect_running(100);
  gs->dashboard_mode = 1;

  expect_exit_within(60, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 1);
}

static void dashboard_setup(void)
{
  showing(6);
  gs->dashboard_ts->dashboard_on = 1;
  head_up();
}

ZTEST(screen_state, test_dashboard_stays_open_while_head_up)
{
  dashboard_setup();
  expect_screen_stays(3000);
  zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

ZTEST(screen_state, test_dashboard_entry_clears_flag_bit_1)
{
  dashboard_setup();
  atomic_set(&dashboard_flags, BIT(1) | BIT(0));

  start_screen_expect_running(10);

  zassert_equal(atomic_get(&dashboard_flags), BIT(0));
}

ZTEST(screen_state, test_dashboard_head_down_closes_to_idle)
{
  dashboard_setup();

  start_screen_expect_running(300);
  head_down();

  expect_task_closed(POLL_MS + 1, "dashboard:imu-trigger-off-imm");
  zassert_equal(dmic_stream_stop_fake.call_count, 1);
  zassert_equal(display_close_screen_fake.arg0_val, 6);
}

ZTEST(screen_state, test_dashboard_head_down_ignored_with_head_up_action_nothing)
{
  dashboard_setup();
  get_dashboard_startup_mode_fake.return_val = 2;

  start_screen_expect_running(100);
  head_down();
  expect_running_for(1000);

  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_double_tap_off_closes_with_head_up)
{
  dashboard_setup();
  gs->double_tap_customize = 4;
  gs->double_tap_dashboard_on = 1;

  start_screen_expect_running(300);
  gs->double_tap_dashboard_on = 0;

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dashboard_hold_keeps_it_open_on_head_down)
{
  dashboard_setup();
  gs->dashboard_ts->dashboard_hold = 1;

  start_screen_expect_running(100);
  head_down();
  expect_running_for(1000);

  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_hold_release_closes_it_even_with_head_up)
{
  dashboard_setup();
  gs->dashboard_ts->dashboard_hold = 1;

  start_screen_expect_running(300);
  gs->dashboard_ts->dashboard_hold = 0;

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dashboard_head_down_returns_to_persist_task)
{
  dashboard_setup();
  persist_task(2, 3); /* countdown: not a task the dashboard switches to */

  start_screen_expect_running(200);
  head_down();

  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 2);
  zassert_true(dmic_stream_stop_fake.call_count >= 1);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_dashboard_switches_to_started_navigation)
{
  dashboard_setup();

  start_screen_expect_running(200);
  persist_task(10, 2);

  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 10);
  zassert_equal(dmic_stream_stop_fake.call_count, 1, "navigation does not use the mic");
}

ZTEST(screen_state, test_dashboard_switch_to_mic_task_keeps_mic)
{
  dashboard_setup();
  persist_task(9, 2);

  screen_start();

  expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 9);
  zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

ZTEST(screen_state, test_dashboard_switches_to_each_known_task_id)
{
  static const uint8_t tasks[] = {7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20};

  for (size_t index = 0; index < ARRAY_SIZE(tasks); index++) {
    dashboard_setup();
    persist_task(tasks[index], 2);
    screen_start();
    expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
    zassert_equal(gs->dashboard_mode, tasks[index], "task %d", tasks[index]);
    screen_stop();
  }
}

/* polls at 0, 51, ... 459 ms */
ZTEST(screen_state, test_dashboard_counts_unknown_synced_task)
{
  dashboard_setup();
  persist_task(2, 2);
  gs->unknown_task_count = 9;

  expect_screen_stays(490);

  zassert_equal(gs->unknown_task_count, 10, "%d", gs->unknown_task_count);
}

/* cmd 6 {2, 9} closes the slave's dashboard */
ZTEST(screen_state, test_dashboard_exit_request_closes_page_then_goes_to_task)
{
  dashboard_setup();
  persist_task(16, 2);
  gs->dashboard_ts->exit_pending = 1;

  start_screen_expect_running(300);
  expect_str(syncs(), "06 02 09");
  zassert_equal(display_close_screen_fake.call_count, 1);
  zassert_equal(display_close_screen_fake.arg0_val, 6);
  zassert_equal(gs->dashboard_ts->exit_pending, 2);

  gs->dashboard_ts->dashboard_on = 0; /* the display thread closed the page */
  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 16);
  zassert_equal(gs->dashboard_ts->exit_pending, 0);
  zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

ZTEST(screen_state, test_dashboard_exit_request_retries_until_slave_acks)
{
  dashboard_setup();
  persist_task(16, 2);
  gs->dashboard_ts->exit_pending = 1;
  sync_to_slave_fake.return_val = 3000;

  start_screen_expect_running(300);
  zassert_true(g1_fake_sync_log.count >= 5, "%d sync attempts", g1_fake_sync_log.count);
  zassert_equal(display_close_screen_fake.call_count, 0);

  sync_to_slave_fake.return_val = 0;
  gs->dashboard_ts->dashboard_on = 0;
  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 16);
}

ZTEST(screen_state, test_dashboard_slave_leaves_exit_request_to_master)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  dashboard_setup();
  persist_task(16, 2);
  gs->dashboard_ts->exit_pending = 1;

  expect_screen_stays(500);

  zassert_equal(gs->dashboard_ts->exit_pending, 1);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_dashboard_head_down_with_exit_pending_goes_idle)
{
  dashboard_setup();
  g1_unit_set_role(G1_ROLE_SLAVE); /* exit_pending stays 1 */
  persist_task(16, 2);
  gs->dashboard_ts->exit_pending = 1;

  start_screen_expect_running(100);
  head_down();

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
  zassert_equal(dmic_stream_stop_fake.call_count, 1);
}

ZTEST(screen_state, test_dashboard_follows_message_screen_request)
{
  dashboard_setup();

  start_screen_expect_running(100);
  gs->dashboard_mode = 4;

  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(dmic_stream_stop_fake.call_count, 1);
}

ZTEST(screen_state, test_dashboard_touch_with_messages_shows_message)
{
  dashboard_setup();
  count_messages_fake.return_val = 2;

  start_screen_expect_running(100);
  *msg_timeout_flag_ptr = 1;

  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 4);
  zassert_equal(g1_fake_msg_show_state, 6);
  zassert_equal(dmic_stream_stop_fake.call_count, 1);
  zassert_equal(set_particle_pending_fake.call_count, 1);
  expect_str(triggers(), "display msg by touch->1 (no change)");
}

ZTEST(screen_state, test_dashboard_touch_without_messages_stays)
{
  dashboard_setup();
  *msg_timeout_flag_ptr = 1;
  expect_screen_stays(500);
}

ZTEST(screen_state, test_dashboard_returns_to_dispatcher_when_suspended)
{
  dashboard_setup();
  gs->suspend_state = 1;

  screen_start();

  expect_exit_within(POLL_MS + 1, 0, NULL);
  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_closes_on_dfu)
{
  dashboard_setup();

  start_screen_expect_running(100);
  gs->work_mode = 1;

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dashboard_wait_blow_head_request_ends_screen)
{
  dashboard_setup();
  expect_wait_blow_head_exit("dashboard:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 1);
}
