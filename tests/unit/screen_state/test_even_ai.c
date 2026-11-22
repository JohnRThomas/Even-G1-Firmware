/* The even AI (16) and even AI v2 (20) answer screens. */
#include "screen_test.h"

static void evenai_setup(void) {
  showing(16);
  persist_task(16, 2);
  gs->dash_buf->type = 3;
  gs->dash_buf->data[0] = 'A';
  get_glasses_ble_status_fake.return_val = 1;
}

ZTEST(screen_state, test_evenai_stays_while_answer_shown) {
  evenai_setup();
  expect_screen_stays(2000);
  zassert_equal(gs->dash_buf->type, 3);
}

ZTEST(screen_state, test_evenai_empty_buffer_closes_and_reports_to_phone) {
  evenai_setup();

  start_screen_expect_running(200);
  gs->dash_buf->data[0] = 0;

  expect_task_closed(POLL_MS + 1, "evenai:self exit");
  zassert_equal(report_event_to_phone_fake.call_count, 1);
  zassert_equal(report_event_to_phone_fake.arg0_val, 0);
  zassert_equal(gs->dash_buf->type, 0);
}

ZTEST(screen_state, test_evenai_slave_closes_without_phone_event) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  evenai_setup();
  gs->dash_buf->type = 0;

  screen_start();

  expect_exit_within(10, RET_TASK_EXITED, "evenai:self exit");
  zassert_equal(report_event_to_phone_fake.call_count, 0);
}

/* type 10: disconnected page */
ZTEST(screen_state, test_evenai_phone_link_lost_switches_to_type_10) {
  evenai_setup();

  start_screen_expect_running(200);
  get_glasses_ble_status_fake.return_val = 0;
  expect_running_for(POLL_MS + 1);

  zassert_equal(gs->dash_buf->type, 10);
  zassert_equal(gs->dashboard_mode, 16);
}

ZTEST(screen_state, test_evenai_timed_answer_closes_after_stay_time) {
  evenai_setup();
  gs->dash_buf->type = 8;
  gs->stay_time_left_s = 3;

  start_screen_expect_running(3 * 20 * 50 - 100);

  expect_exit_within(3 * 20 * POLL_MS - since_start() + POLL_MS + 5, RET_TASK_EXITED, "evenai:self exit");
  zassert_equal(gs->stay_time_left_s, 0);
  zassert_equal(report_event_to_phone_fake.arg0_val, 0);
}

/* the counter wraps to 255 after 16 s; nothing reads it for type 7 */
ZTEST(screen_state, test_evenai_type_7_page_is_not_closed_by_stay_time) {
  evenai_setup();
  gs->dash_buf->type = 7;
  gs->stay_time_left_s = 16;

  expect_screen_stays(20000);

  zassert_equal(gs->dash_buf->type, 7);
}

ZTEST(screen_state, test_evenai_closes_in_silent_mode) {
  evenai_setup();

  start_screen_expect_running(100);
  gs->display_mode = 12;

  expect_task_closed(POLL_MS + 1, "evenai:self exit");
  zassert_equal(gs->dash_buf->data[0], 0);
}

ZTEST(screen_state, test_evenai_closes_on_dfu) {
  evenai_setup();
  expect_dfu_exit(1, "evenai:self exit");
  zassert_equal(gs->dash_buf->type, 0);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_evenai_wait_blow_head_request_clears_buffer) {
  evenai_setup();
  expect_wait_blow_head_exit("evenai:imu-trigger-off-imm");
  zassert_equal(gs->dash_buf->type, 0);
  zassert_equal(gs->dash_buf->data[0], 0);
}

static void evenai_v2_setup(void) {
  showing(20);
  persist_task(20, 2);
  gs->evenai_buf->state = 1;
  gs->evenai_buf->sub = 3;
}

ZTEST(screen_state, test_even_ai_v2_entry_stamps_time_and_drops_exit_request) {
  evenai_v2_setup();
  gs->dashboard_ts->exit_pending = 1;
  g1_clock_advance_ms(1234);

  start_screen_expect_running(100);

  zassert_equal(gs->evenai_buf->ts, screen_start_ms);
  zassert_equal(gs->dashboard_ts->exit_pending, 0);
}

ZTEST(screen_state, test_even_ai_v2_slave_entry_gives_sem_2) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  evenai_v2_setup();

  start_screen_expect_running(100);

  zassert_equal(k_sem_count_get(&gs->sem_2), 1);
}

ZTEST(screen_state, test_even_ai_v2_finished_closes_to_idle) {
  evenai_v2_setup();

  start_screen_expect_running(300);
  gs->evenai_buf->state = 0;

  expect_task_closed(POLL_MS + 1, "evenai_v2:self exit");
}

ZTEST(screen_state, test_even_ai_v2_master_state_2_closes) {
  evenai_v2_setup();
  gs->evenai_buf->state = 2;

  screen_start();

  expect_exit_within(10, RET_TASK_EXITED, "evenai_v2:self exit");
  zassert_equal(gs->evenai_buf->state, 3);
}

ZTEST(screen_state, test_even_ai_v2_slave_state_2_stays) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  evenai_v2_setup();
  gs->evenai_buf->state = 2;

  start_screen_expect_running(500);

  zassert_equal(gs->evenai_buf->state, 2);
}

ZTEST(screen_state, test_even_ai_v2_listening_times_out_after_15s) {
  evenai_v2_setup();
  gs->evenai_buf->sub = 2;

  start_screen_expect_running(15000);
  zassert_equal(gs->evenai_buf->state, 1);

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
  zassert_equal(gs->evenai_buf->state, 0);
}

ZTEST(screen_state, test_even_ai_v2_sub7_action1_times_out_after_5s) {
  evenai_v2_setup();
  gs->evenai_buf->sub = 7;
  gs->evenai_buf->action_status = 1;

  start_screen_expect_running(5000);

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
}

ZTEST(screen_state, test_even_ai_v2_sub4_action13_times_out_after_5s) {
  evenai_v2_setup();
  gs->evenai_buf->sub = 4;
  gs->evenai_buf->action_status = 0x13;

  start_screen_expect_running(5000);

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
}

ZTEST(screen_state, test_even_ai_v2_answer_does_not_time_out) {
  evenai_v2_setup();

  start_screen_expect_running(20000);

  zassert_equal(gs->evenai_buf->state, 1);
}

ZTEST(screen_state, test_even_ai_v2_slave_listening_does_not_time_out) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  evenai_v2_setup();
  gs->evenai_buf->sub = 2;

  start_screen_expect_running(20000);

  zassert_equal(gs->evenai_buf->state, 1);
}

ZTEST(screen_state, test_even_ai_v2_close_retried_until_slave_acks) {
  evenai_v2_setup();
  gs->evenai_buf->state = 0;
  sync_to_slave_fake.return_val = 3000;

  start_screen_expect_running(300);
  zassert_true(g1_fake_sync_log.count >= 5, "%d attempts", g1_fake_sync_log.count);
  zassert_equal(gs->dashboard_mode, 20);

  sync_to_slave_fake.return_val = 0;
  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_even_ai_v2_closes_on_dfu) {
  evenai_v2_setup();
  expect_dfu_exit(1, "evenai_v2:dfu");
}

ZTEST(screen_state, test_even_ai_v2_wait_blow_head_request_ends_screen) {
  evenai_v2_setup();
  expect_wait_blow_head_exit("evenai_v2:imu-trigger-off-imm");
}
