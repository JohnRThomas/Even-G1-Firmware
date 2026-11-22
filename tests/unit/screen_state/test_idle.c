/* screen_task_dispatch() and the idle screen (0). */
#include "screen_test.h"

/* 500 ms master exit delay + 150 ms dispatch sleep */
ZTEST(screen_state, test_dispatch_turns_screen_off_with_handler_reason) {
  showing(2);
  persist_task(2, 2);

  dispatch_start();

  expect_exit_within(1000, 0, NULL);
  expect_str(triggers(), "countdown:trigger-off-imm->0");
  zassert_equal(gs->screen_on, 0);
  zassert_equal(gs->dashboard_mode, 0);
  zassert_true(done_after() >= 500 + 150, "returned after %d ms", (int)done_after());
}

ZTEST(screen_state, test_dispatch_wakes_screen_for_running_task) {
  gs->dashboard_mode = 9; /* not ready: exits at once */
  gs->screen_on = 0;

  dispatch_start();

  expect_exit_within(2000, 0, NULL);
  expect_str(triggers(), "BLE:wakeup:running_task->1; temeprompter:self exit->0");
}

ZTEST(screen_state, test_dispatch_follows_screen_changes_until_a_task_exits) {
  showing(2);
  persist_task(2, 2);
  gs->countdown_ts->enabled = 1;
  gs->countdown_ts->seconds = 60;
  head_up();

  dispatch_start();
  expect_running_for(300);
  zassert_equal(gs->dashboard_mode, 6);

  head_down();
  expect_exit_within(500, 0, NULL);
  expect_str(triggers(), "dashboard:imu-trigger-off-imm->0");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dispatch_unknown_screen_sleeps_1s_and_returns) {
  showing(3);

  dispatch_start();

  expect_running_for(990);
  expect_exit_within(20, 0, NULL);
  expect_str(triggers(), "");
  zassert_equal(gs->dashboard_mode, 3);
}

ZTEST(screen_state, test_dispatch_expires_messages_and_releases_msg_busy) {
  showing(3);
  msg_busy = 1;

  dispatch_start();
  expect_exit_within(1100, 0, NULL);

  zassert_equal(clear_timeout_message_fake.call_count, 1);
  zassert_equal(clear_timeout_message_fake.arg0_val, 0);
  zassert_equal(msg_busy, 0);
}

ZTEST(screen_state, test_dispatch_keeps_msg_busy_for_message_screen) {
  showing(4);
  get_msg_sync_state_fake.return_val = 4;
  msg_busy = 1;
  head_up();

  dispatch_start();
  expect_running_for(100);

  zassert_equal(msg_busy, 1);
}

ZTEST(screen_state, test_dispatch_master_hides_expired_message_left_showing) {
  showing(3);
  g1_fake_msg_show_state = 5;
  cur_message_show_state_fake.return_val = 2;

  dispatch_start();
  expect_exit_within(1100, 0, NULL);

  expect_str(syncs(), "07 02");
  zassert_equal(g1_fake_msg_show_state, 0);
}

ZTEST(screen_state, test_dispatch_slave_leaves_expired_message_to_master) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  showing(3);
  g1_fake_msg_show_state = 5;
  cur_message_show_state_fake.return_val = 2;

  dispatch_start();
  expect_exit_within(1100, 0, NULL);

  expect_str(syncs(), "");
  zassert_equal(g1_fake_msg_show_state, 5);
}

/* 6 = unread icon */
ZTEST(screen_state, test_dispatch_exit_with_new_message_shows_unread_icon) {
  showing(2);
  persist_task(2, 2);
  g1_fake_msg_show_state = 1;
  gs->unread_msg_num = 2;

  dispatch_start();
  expect_exit_within(1000, 0, NULL);

  zassert_equal(g1_fake_msg_show_state, 6);
}

static void idle_setup(void) {
  gs->dashboard_mode = 0;
  gs->screen_on = 1;
  gs->suspend_state = 1;
  is_ble_device_bound_fake.return_val = true;
  get_glasses_ble_status_fake.return_val = 1;
}

ZTEST(screen_state, test_idle_entry_turns_screen_off_and_clears_task_record) {
  idle_setup();
  persist_task(9, 1);
  atomic_set(&dashboard_flags, BIT(1) | BIT(0));
  gs->dash_buf->type = 5;
  gs->dash_buf->data[0] = 'A';
  *msg_timeout_flag_ptr = 1;

  start_screen_expect_running(200);

  expect_str(triggers(), "goto idle1->0");
  zassert_equal(gs->screen_on, 0);
  zassert_equal(gs->dashboard_mode, 0);
  zassert_equal(gs->task_status->task, 0);
  zassert_equal(gs->task_status->status, 1);
  zassert_equal(atomic_get(&dashboard_flags), BIT(0), "flags 0x%lx", (long)atomic_get(&dashboard_flags));
  zassert_equal(gs->dash_buf->type, 0);
  zassert_equal(gs->dash_buf->data[0], 0);
  zassert_equal(*msg_timeout_flag_ptr, 0);
}

ZTEST(screen_state, test_idle_entry_sets_message_icon_state_from_unread_count) {
  idle_setup();
  g1_fake_msg_show_state = 2;
  gs->unread_msg_num = 3;

  start_screen_expect_running(100);

  zassert_equal(g1_fake_msg_show_state, 6);
}

ZTEST(screen_state, test_idle_entry_keeps_showing_message_in_state_5) {
  idle_setup();
  cur_message_show_state_fake.return_val = 1;

  start_screen_expect_running(100);

  zassert_equal(g1_fake_msg_show_state, 5);
}

ZTEST(screen_state, test_idle_slave_entry_gives_sem_2) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  idle_setup();

  start_screen_expect_running(100);

  zassert_equal(k_sem_count_get(&gs->sem_2), 1);
}

/* the master sets the 80 ms (0x50) IMU period on every idle poll; a 10 ms wait takes 11 ms here */
static int64_t first_idle_poll_ms;

static void record_first_poll(struct imu_ctx *ctx, uint32_t period_ms) {
  if (period_ms == 0x50 && first_idle_poll_ms < 0) {
    first_idle_poll_ms = since_start();
  }
  ctx->sample_period_ms = period_ms;
}

ZTEST(screen_state, test_idle_waits_for_panel_suspend_before_polling) {
  idle_setup();
  gs->suspend_state = 0;
  first_idle_poll_ms = -1;
  g1_fake_imu_set_fake.custom_fake = record_first_poll;

  screen_start();
  g1_clock_advance_ms(300);
  zassert_equal(first_idle_poll_ms, -1, "polled at %d ms before the suspend", (int)first_idle_poll_ms);
  gs->suspend_state = 1;
  expect_running_for(100);

  zassert_between_inclusive(first_idle_poll_ms, 300, 311, "first poll at %d ms", (int)first_idle_poll_ms);
  zassert_equal(gs->imu.sample_period_ms, 0x50);
}

ZTEST(screen_state, test_idle_gives_up_waiting_for_panel_suspend_after_1s) {
  idle_setup();
  gs->suspend_state = 0;
  first_idle_poll_ms = -1;
  g1_fake_imu_set_fake.custom_fake = record_first_poll;

  start_screen_expect_running(1200);

  zassert_between_inclusive(first_idle_poll_ms, 1000, 100 * 11 + 11, "first poll at %d ms", (int)first_idle_poll_ms);
}

/* canvas sync 0x0c: {0x0b, distance gear, height offset} */
ZTEST(screen_state, test_idle_head_up_opens_dashboard_with_canvas_sync_and_mic) {
  idle_setup();
  gs->screen_on = 0;
  gs->panel.canvas_distance_gear = 5;
  gs->panel.raster_height_offset = 3;
  head_up();

  screen_start();

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 6);
  zassert_equal(gs->dashboard_ts->dashboard_on, 1);
  expect_str(triggers(), "IMU:wakeup:dashboard->1");
  expect_str(syncs(), "0c 0b 05 03");
  zassert_equal(cal_panel_canvas_coord_fake.call_count, 1);
  zassert_equal(request_dmic_stream_start_fake.call_count, 1);
}

/* head up changes the screen without starting the mic */
static void expect_head_up_starts_no_mic(void) {
  head_up();
  screen_start();
  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(request_dmic_stream_start_fake.call_count, 0);
}

ZTEST(screen_state, test_idle_head_up_without_phone_link_starts_no_mic) {
  idle_setup();
  get_glasses_ble_status_fake.return_val = 0;
  expect_head_up_starts_no_mic();
  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_head_up_with_head_up_mic_disabled_starts_no_mic) {
  idle_setup();
  gs->head_up_mic_disabled = 1;
  expect_head_up_starts_no_mic();
}

ZTEST(screen_state, test_idle_head_up_with_mic_running_does_not_restart_it) {
  idle_setup();
  G1_FAKE_RETURN(dmic_get_stop_state, 0);
  expect_head_up_starts_no_mic();
}

ZTEST(screen_state, test_idle_slave_head_up_opens_dashboard_without_sync_or_mic) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  idle_setup();
  expect_head_up_starts_no_mic();
  zassert_equal(gs->dashboard_mode, 6);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_idle_head_up_with_head_up_action_nothing_stays_idle) {
  idle_setup();
  get_dashboard_startup_mode_fake.return_val = 2; /* head up does nothing */
  head_up();

  start_screen_expect_running(200);
  expect_str(triggers(), "");
  zassert_equal(gs->screen_on, 1);

  expect_running_for(300);
  zassert_equal(gs->dashboard_mode, 0);
}

/* customize 4: double tap toggles the dashboard */
ZTEST(screen_state, test_idle_double_tap_dashboard_on_opens_dashboard) {
  idle_setup();
  gs->double_tap_customize = 4;
  gs->double_tap_dashboard_on = 1;

  screen_start();

  expect_exit_within(1100, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_dashboard_hold_opens_dashboard) {
  idle_setup();
  gs->dashboard_ts->dashboard_hold = 1;

  screen_start();

  expect_exit_within(1100, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 6);
}

/* with setting at value, head up neither opens the dashboard nor leaves the idle screen */
#define HEAD_UP_IGNORED_TEST(name, setting, value)                                                                     \
  ZTEST(screen_state, test_##name) {                                                                                   \
    idle_setup();                                                                                                      \
    setting = (value);                                                                                                 \
    head_up();                                                                                                         \
    start_screen_expect_running(500);                                                                                  \
    zassert_equal(gs->dashboard_mode, 0);                                                                              \
    zassert_equal(gs->dashboard_ts->dashboard_on, 0);                                                                  \
  }

HEAD_UP_IGNORED_TEST(idle_head_up_ignored_in_case, is_in_box_fake.return_val, 1)
HEAD_UP_IGNORED_TEST(idle_head_up_ignored_in_silent_mode, gs->display_mode, 12)
HEAD_UP_IGNORED_TEST(idle_head_up_ignored_in_production_mode, get_pt_test_mode_fake.return_val, 1)
HEAD_UP_IGNORED_TEST(idle_head_up_ignored_when_not_bound, is_ble_device_bound_fake.return_val, 0)
HEAD_UP_IGNORED_TEST(idle_head_up_ignored_during_onboarding, gs->onboarding->in_progress, 1)
HEAD_UP_IGNORED_TEST(idle_head_up_ignored_while_bitmap_syncs, gs->bitmap_info->sync_pending, 1)

/* 0x26 = 38 ms IMU period */
ZTEST(screen_state, test_idle_resumes_running_persist_task) {
  idle_setup();
  gs->screen_on = 0;
  persist_task(10, 2);
  gs->dashboard_ts->exit_pending = 1;
  gs->evenai_buf->state = 2;

  screen_start();

  expect_exit_within(60, 0, NULL);
  zassert_equal(gs->dashboard_mode, 10);
  expect_str(triggers(), "BLE:wakeup:persist_task->1");
  zassert_equal(gs->dashboard_ts->exit_pending, 0);
  zassert_equal(gs->evenai_buf->state, 0);
  zassert_equal(g1_fake_imu_set_fake.arg1_val, 0x26);
}

ZTEST(screen_state, test_idle_resumes_paused_persist_task) {
  idle_setup();
  persist_task(9, 3);

  screen_start();

  expect_exit_within(60, 0, NULL);
  zassert_equal(gs->dashboard_mode, 9);
}

ZTEST(screen_state, test_idle_does_not_resume_persist_task_in_case) {
  idle_setup();
  is_in_box_fake.return_val = 1;
  persist_task(10, 2);
  expect_screen_stays(500);
}

ZTEST(screen_state, test_idle_silent_mode_does_not_resume_other_persist_task) {
  idle_setup();
  gs->display_mode = 12;
  persist_task(10, 2);
  expect_screen_stays(500);
}

ZTEST(screen_state, test_idle_dfu_returns_to_dispatcher) {
  idle_setup();
  gs->work_mode = 1;

  screen_start();

  expect_exit_within(60, 0, NULL);
  zassert_equal(gs->dashboard_mode, 0);
  zassert_equal(g1_fake_imu_set_fake.arg1_val, 0x26);
}

ZTEST(screen_state, test_idle_case_dfu_returns_to_dispatcher) {
  idle_setup();
  gs->work_mode = 8;

  screen_start();

  expect_exit_within(60, 0, NULL);
}

ZTEST(screen_state, test_idle_waits_4s_for_pending_dashboard_exit) {
  idle_setup();
  gs->dashboard_ts->exit_pending = 1;
  head_up();

  start_screen_expect_running(3990);
  zassert_equal(gs->dashboard_ts->exit_pending, 1);
  zassert_equal(gs->dashboard_mode, 0);

  expect_exit_within(80 * POLL_MS - 3990 + 60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_ts->exit_pending, 0);
  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_waits_5s_for_even_ai_v2) {
  idle_setup();
  gs->evenai_buf->state = 2;
  head_up();

  start_screen_expect_running(4990);
  zassert_equal(gs->evenai_buf->state, 2);

  expect_exit_within(100 * POLL_MS - 4990 + 60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->evenai_buf->state, 0);
  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_head_up_returns_to_message_still_showing) {
  idle_setup();
  gs->screen_on = 0;
  is_message_showing_fake.return_val = true;
  count_unread_messages_fake.return_val = 1;
  head_up();

  screen_start();

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 4);
  expect_str(triggers(), "IMU:wakeup:uncomplete msg->1");
  expect_str(syncs(), "07 01");
  zassert_equal(clear_timeout_message_fake.arg0_val, 0);
  zassert_equal(mark_cur_message_read_fake.call_count, 1);
  zassert_equal(set_particle_pending_fake.call_count, 1);
  zassert_equal(*msg_timeout_flag_ptr, 1);
  zassert_equal(gs->dashboard_ts->dashboard_on, 0);
}

ZTEST(screen_state, test_idle_new_message_opens_new_message_screen) {
  idle_setup();

  start_screen_expect_running(100);
  g1_fake_msg_show_state = 1;

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 5);
  expect_str(triggers(), "goto idle1->0; BLE:wakeup:new-unread_msg->1");
  expect_str(syncs(), "07 00");
}

ZTEST(screen_state, test_idle_slave_new_message_waits_for_master) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  idle_setup();

  start_screen_expect_running(100);
  g1_fake_msg_show_state = 1;
  expect_running_for(300);
  zassert_equal(gs->dashboard_mode, 0);

  gs->dashboard_mode = 5;
  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 5);
}

ZTEST(screen_state, test_idle_auto_display_shows_new_message_as_message_screen) {
  idle_setup();
  gs->message_mode = 1;

  start_screen_expect_running(100);
  g1_fake_msg_show_state = 1;

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 4);
  expect_str(triggers(), "goto idle1->0; IMU:wakeup:uncomplete msg->1");
  expect_str(syncs(), "07 01");
}

ZTEST(screen_state, test_idle_without_input_stays_idle) {
  idle_setup();

  expect_screen_stays(3000);

  zassert_true(g1_fake_imu_set_fake.call_count >= 55, "%d polls in 3 s", g1_fake_imu_set_fake.call_count);
}
