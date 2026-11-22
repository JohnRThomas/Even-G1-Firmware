/* message_task.c's message helpers and the message (4), unread (8) and new message (5) screens. */
#include "screen_test.h"

ZTEST(screen_state, test_reset_dash_buf_clear_empties_buffer_and_esb_counters) {
  struct dash_data_buf *buf = gs->dash_buf;

  gs->esb_up_seq = 1;
  gs->esb_down_seq = 2;
  gs->esb_up_id = 3;
  gs->esb_down_id = 4;
  gs->esb_up_recving = 1;
  gs->esb_down_recving = 1;
  buf->type = 8;
  memset(buf->data, 'x', sizeof(buf->data));
  buf->len = 0x1234;

  reset_dash_buf(1);

  zassert_equal(gs->esb_up_seq, 0);
  zassert_equal(gs->esb_down_seq, 0);
  zassert_equal(gs->esb_up_id, 0);
  zassert_equal(gs->esb_down_id, 0);
  zassert_equal(gs->esb_up_recving, 0);
  zassert_equal(gs->esb_down_recving, 0);
  zassert_equal(buf->type, 0);
  expect_fill(buf->data, sizeof(buf->data), 0, "data");
  zassert_equal(buf->len, 0x1234);
}

ZTEST(screen_state, test_reset_dash_buf_keep_marks_first_byte_13_and_keeps_type) {
  struct dash_data_buf *buf = gs->dash_buf;

  buf->type = 3;
  memset(buf->data, 'x', sizeof(buf->data));
  gs->esb_up_recving = 1;

  reset_dash_buf(0);

  zassert_equal(buf->type, 3);
  zassert_equal(buf->data[0], 13);
  zassert_equal(buf->data[1], 0);
  zassert_equal(buf->data[sizeof(buf->data) - 1], 0);
  zassert_equal(gs->esb_up_recving, 0);
}

ZTEST(screen_state, test_msg_timeout_flag_needs_unread_messages_and_is_taken_once) {
  set_msg_timeout_flag();
  zassert_equal(take_msg_timeout_flag(), 0, "flag set without unread messages");

  count_unread_messages_fake.return_val = 2;
  set_msg_timeout_flag();
  zassert_equal(take_msg_timeout_flag(), 1);
  zassert_equal(take_msg_timeout_flag(), 0, "flag not cleared by take");
}

/* 31536000 s = one year */
ZTEST(screen_state, test_msg_stay_time_per_display_mode) {
  zassert_equal(get_msg_stay_time(10, 7), 7);
  zassert_equal(get_msg_stay_time(11, 7), 31536000);
  zassert_equal(get_msg_stay_time(12, 7), 1);
  zassert_equal(get_msg_stay_time(0, 9), 9);
}

ZTEST(screen_state, test_sync_message_signal_sends_cmd_7_with_signal) {
  sync_to_slave_fake.return_val = 12;

  zassert_equal(sync_message_signal_to_slave(1), 12);
  expect_str(syncs(), "07 01");
}

ZTEST(screen_state, test_touch_with_unread_messages_marks_read_and_flags_touch) {
  count_unread_messages_fake.return_val = 2;

  handle_msg_touch_key();

  zassert_equal(gs->unread_msg_num, 2);
  zassert_equal(clear_timeout_message_fake.call_count, 1);
  zassert_equal(clear_timeout_message_fake.arg0_val, 1);
  zassert_equal(mark_cur_message_read_fake.call_count, 1);
  zassert_equal(take_msg_timeout_flag(), 1);
}

ZTEST(screen_state, test_touch_on_shown_message_restarts_stay_time) {
  count_unread_messages_fake.return_val = 1;
  g1_fake_msg_show_state = 4;
  gs->message_stay_time = 7;
  gs->stay_time_left_s = 1;

  handle_msg_touch_key();

  zassert_equal(gs->stay_time_left_s, 7);
}

ZTEST(screen_state, test_touch_without_unread_wakes_dashboard_when_head_up) {
  g1_fake_msg_show_state = 2;
  gs->stay_time_left_s = 5;
  head_up();

  handle_msg_touch_key();

  zassert_equal(g1_fake_msg_show_state, 0);
  expect_str(triggers(), "IMU:wakeup:dashboard->1");
  zassert_equal(gs->screen_on, 1);
  zassert_equal(gs->stay_time_left_s, 0);
  zassert_equal(take_msg_timeout_flag(), 0);
}

ZTEST(screen_state, test_touch_without_unread_head_down_keeps_screen_off) {
  g1_fake_msg_show_state = 2;

  handle_msg_touch_key();

  zassert_equal(g1_fake_msg_show_state, 0);
  expect_str(triggers(), "");
  zassert_equal(gs->screen_on, 0);
}

static void message_setup(void) {
  showing(4);
  gs->unread_msg_num = 1;
  get_msg_sync_state_fake.return_val = 4;
  gs->message_stay_time = 5;
}

/* auto display (message_mode 1); cur_message_show_state() 1 on display, 2 expired */
static void auto_display_setup(uint8_t show_state) {
  message_setup();
  gs->message_mode = 1;
  cur_message_show_state_fake.return_val = show_state;
}

ZTEST(screen_state, test_message_show_entry_sets_show_state_and_stay_time) {
  message_setup();
  gs->unknown_task_count = 7;
  head_up();

  start_screen_expect_running(100);

  zassert_equal(g1_fake_msg_show_state, 4);
  zassert_equal(gs->stay_time_left_s, 5);
  zassert_equal(gs->unknown_task_count, 0);
  zassert_equal(get_msg_sync_state_fake.arg0_val, 0, "newest unread is index 0");
}

/* 1000 polls of 1 ms, 2 ms each here */
ZTEST(screen_state, test_message_show_waits_up_to_1s_for_slave_copy) {
  message_setup();
  get_msg_sync_state_fake.return_val = 2;
  head_up();

  start_screen_expect_running(990);
  zassert_equal(g1_fake_msg_show_state, 0);

  expect_running_for(1000 * 2 - 990 + 10);
  zassert_equal(g1_fake_msg_show_state, 4);
}

ZTEST(screen_state, test_message_show_stays_while_head_up_without_auto_display) {
  message_setup();
  head_up();

  expect_screen_stays(10000);

  zassert_equal(gs->stay_time_left_s, 5);
}

ZTEST(screen_state, test_message_show_head_down_closes_to_idle) {
  message_setup();
  head_up();

  start_screen_expect_running(300);
  head_down();

  expect_task_closed(SELF_EXIT_MS, "imu-trigger-off-imm");
  zassert_equal(g1_fake_msg_show_state, 5);
  zassert_equal(display_close_screen_fake.arg0_val, 4);
}

/* the master counts 1 s per 20 polls while stay < 30 */
ZTEST(screen_state, test_message_show_auto_display_closes_after_stay_time) {
  auto_display_setup(1);

  start_screen_expect_running(5 * 20 * 50 - 100);
  zassert_true(gs->stay_time_left_s <= 1, "stay %d", gs->stay_time_left_s);

  expect_exit_within(5 * 20 * POLL_MS - since_start() + POLL_MS + 501 + 10, RET_TASK_EXITED, "imu-trigger-off-imm");
  zassert_equal(gs->stay_time_left_s, 0);
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_message_show_auto_display_long_stay_waits_for_expiry) {
  auto_display_setup(1);
  gs->message_stay_time = 40;

  start_screen_expect_running(5000);
  zassert_equal(gs->stay_time_left_s, 40);

  cur_message_show_state_fake.return_val = 2; /* expired */
  expect_exit_within(SELF_EXIT_MS, RET_TASK_EXITED, "imu-trigger-off-imm");
}

ZTEST(screen_state, test_message_show_slave_does_not_count_stay_time) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  auto_display_setup(1);

  start_screen_expect_running(7000);

  zassert_equal(gs->stay_time_left_s, 5);
}

ZTEST(screen_state, test_message_show_expired_with_head_up_goes_to_dashboard) {
  auto_display_setup(2);
  gs->last_sync_ts = 5000;
  gs->dashboard_ts->exit_pending = 1;
  head_up();

  screen_start();

  expect_exit_within(501 + 10, RET_SCREEN_CHANGED, NULL);
  zassert_true(done_after() >= 500);
  zassert_equal(gs->dashboard_mode, 6);
  zassert_equal(gs->dashboard_ts->dashboard_on, 1);
  zassert_equal(gs->last_sync_ts, 4000);
  zassert_equal(gs->dashboard_ts->exit_pending, 0);
  expect_str(syncs(), "0c 0b 00 00");
}

ZTEST(screen_state, test_message_show_expired_with_head_up_action_nothing_goes_idle) {
  auto_display_setup(2);
  get_dashboard_startup_mode_fake.return_val = 2;
  head_up();

  screen_start();

  expect_exit_within(501 + 10, RET_TASK_EXITED, "imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_message_show_expired_returns_to_double_tap_dashboard) {
  auto_display_setup(2);
  gs->double_tap_customize = 4;
  gs->double_tap_dashboard_on = 1;

  screen_start();

  expect_exit_within(501 + 10, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_message_show_slave_waits_for_master_to_close) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  message_setup();

  start_screen_expect_running(500);
  zassert_equal(g1_fake_msg_show_state, 4);

  gs->dashboard_mode = 0;
  expect_exit_within(60, RET_TASK_EXITED, "imu-trigger-off-imm");
  zassert_equal(g1_fake_msg_show_state, 5);
}

ZTEST(screen_state, test_message_show_closes_in_silent_mode) {
  message_setup();
  head_up();

  start_screen_expect_running(100);
  gs->display_mode = 12;

  expect_exit_within(60, RET_TASK_EXITED, "imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
  zassert_equal(g1_fake_msg_show_state, 5);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_message_show_closes_on_dfu) {
  message_setup();
  head_up();
  expect_dfu_exit(8, "imu-trigger-off-imm");
  zassert_equal(g1_fake_msg_show_state, 5);
}

ZTEST(screen_state, test_message_show_follows_screen_change) {
  message_setup();
  head_up();

  start_screen_expect_running(100);
  gs->dashboard_mode = 6;

  expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
}

ZTEST(screen_state, test_message_show_screen_set_idle_exits) {
  message_setup();
  head_up();

  start_screen_expect_running(100);
  gs->dashboard_mode = 0;

  expect_exit_within(60, RET_TASK_EXITED, "imu-trigger-off-imm");
}

static void unread_setup(void) {
  showing(8);
  gs->schedule_notif_pending = 1;
  count_unread_messages_fake.return_val = 3;
  head_up();
}

ZTEST(screen_state, test_unread_msg_entry_takes_schedule_flag_and_counts_unread) {
  unread_setup();

  start_screen_expect_running(500);

  zassert_equal(gs->schedule_notif_pending, 0);
  zassert_equal(gs->unread_msg_num, 3);
}

ZTEST(screen_state, test_unread_msg_head_down_closes_to_idle) {
  unread_setup();

  start_screen_expect_running(300);
  head_down();

  expect_task_closed(SELF_EXIT_MS, "imu-trigger-off-imm");
  zassert_equal(display_close_screen_fake.arg0_val, 8);
}

ZTEST(screen_state, test_unread_msg_closes_on_dfu) {
  unread_setup();

  start_screen_expect_running(100);
  gs->work_mode = 1;

  expect_exit_within(1000, RET_TASK_EXITED, "imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
}

static void new_message_setup(void) {
  showing(5);
  gs->unread_msg_num = 1;
  count_unread_messages_fake.return_val = 1;
  g1_fake_msgs[0].recv_time = 1000;
  gs->message_stay_time = 3;
}

ZTEST(screen_state, test_new_message_entry_sets_show_state_and_stay_time) {
  new_message_setup();

  start_screen_expect_running(100);

  zassert_equal(g1_fake_msg_show_state, 2);
  zassert_equal(gs->stay_time_left_s, 3);
}

ZTEST(screen_state, test_new_message_times_out_after_stay_time) {
  new_message_setup();

  start_screen_expect_running(3 * 20 * 50 - 100);
  zassert_equal(gs->stay_time_left_s, 1);

  expect_task_closed(3 * 20 * POLL_MS - since_start() + 5, "new-msg-timeout-goto-idle");
  zassert_equal(g1_fake_msg_show_state, 6);
}

ZTEST(screen_state, test_new_message_head_up_shows_message) {
  new_message_setup();

  start_screen_expect_running(500);
  head_up();

  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 4);
  expect_str(syncs(), "07 01");
  zassert_equal(clear_timeout_message_fake.arg0_val, 0);
  zassert_equal(*msg_timeout_flag_ptr, 1);
  zassert_equal(mark_cur_message_read_fake.call_count, 1);
  zassert_equal(set_particle_pending_fake.call_count, 1);
  expect_str(triggers(), "IMU:wakeup:new-notification->1 (no change)");
}

ZTEST(screen_state, test_new_message_display_mode_11_shows_message) {
  new_message_setup();
  gs->display_mode = 11;

  screen_start();

  expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 4);
}

/* Run directly, not through the dispatcher. */
ZTEST(screen_state, test_new_message_head_up_with_schedule_opens_unread_screen) {
  new_message_setup();
  gs->schedule_notif_pending = 1;
  head_up();

  screen_start();

  expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
  zassert_equal(gs->dashboard_mode, 8);
  zassert_equal(g1_fake_msg_show_state, 4);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_new_message_slave_waits_for_master) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  new_message_setup();
  head_up();

  start_screen_expect_running(5000);
  zassert_equal(gs->stay_time_left_s, 3);
  zassert_equal(gs->dashboard_mode, 5);

  gs->dashboard_mode = 4;
  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
}

/* decrements at 3, 4 and 5 s on the same 20-poll grid */
ZTEST(screen_state, test_new_message_newer_message_restarts_stay_time) {
  new_message_setup();

  start_screen_expect_running(2500);
  zassert_equal(gs->stay_time_left_s, 1);
  g1_fake_msgs[1].recv_time = 1002;
  gs->unread_msg_num = 2;
  expect_running_for(POLL_MS + 1);
  zassert_equal(gs->stay_time_left_s, 3);

  expect_running_for(4900 - since_start());
  expect_exit_within(5 * 20 * POLL_MS - since_start() + 5, RET_TASK_EXITED, "new-msg-timeout-goto-idle");
}

ZTEST(screen_state, test_new_message_without_unread_reads_inside_queue) {
  new_message_setup();
  gs->unread_msg_num = 0;

  start_screen_expect_running(200);

  zassert_equal(g1_fake_msg_bad_index, 0, "%d reads of entry -1", g1_fake_msg_bad_index);
}

ZTEST(screen_state, test_new_message_closes_in_silent_mode) {
  new_message_setup();

  start_screen_expect_running(100);
  gs->display_mode = 12;

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "message:imu-trigger-off-imm");
  zassert_equal(gs->dashboard_mode, 0);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_new_message_closes_on_dfu) {
  new_message_setup();
  expect_dfu_exit(1, "message:imu-trigger-off-imm");
}

ZTEST(screen_state, test_new_message_closes_on_case_dfu) {
  new_message_setup();
  gs->work_mode = 8;

  screen_start();

  expect_exit_within(10, RET_TASK_EXITED, "message:imu-trigger-off-imm");
}

ZTEST(screen_state, test_new_message_follows_screen_change) {
  new_message_setup();

  start_screen_expect_running(100);
  gs->dashboard_mode = 6;

  expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
}
