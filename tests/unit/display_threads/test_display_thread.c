/*
 * The display thread is cooperative like ztest, so it runs only while the test sleeps or yields:
 * every check sees a settled state.
 */
#include "fakes.h"
#include "harness.h"

#include "display/display_thread.c"

#define PRIO_NORMAL  (-14)
#define PRIO_BOOSTED (-16)

/* every page call, in order */
static struct ui_call {
  const char *page;
  int event;
  void *panel;
  void *arg;
  int64_t uptime_ms;
} ui_calls[64];
static int ui_call_count;
static int page_result;

/* runs on the display thread: no zassert here */
static void ui_log(const char *page, int event, void *panel, void *arg) {
  if (ui_call_count < (int)ARRAY_SIZE(ui_calls)) {
    ui_calls[ui_call_count] = (struct ui_call){page, event, panel, arg, k_uptime_get()};
    ui_call_count++;
  }
}

static int count_ui_calls(const char *page, int event) {
  int count = 0;

  for (int index = 0; index < ui_call_count; index++) {
    if (strcmp(ui_calls[index].page, page) == 0 && ui_calls[index].event == event) {
      count++;
    }
  }
  return count;
}

/* each page stand-in logs its call and returns page_result */
#define UI_PAGE(page)                                                                                                  \
  int page(struct jbd_panel_context *panel, void *arg, int event) {                                                    \
    ui_log(#page, event, panel, arg);                                                                                  \
    return page_result;                                                                                                \
  }

UI_PAGE(ui_demo_image_task)
UI_PAGE(ui_bitmap_task)
UI_PAGE(ui_teleprompter_task)
UI_PAGE(ui_DashBoard_task)
UI_PAGE(ui_QuickNote_task)
UI_PAGE(ui_translate_task)
UI_PAGE(ui_even_ai_task)
UI_PAGE(ui_even_ai_v2_info_task)
UI_PAGE(ui_navigation_task)
UI_PAGE(ui_ancs_notificaton_task)
UI_PAGE(ui_new_message_come_on_task)
UI_PAGE(ui_onboarding_task)
UI_PAGE(ui_raster_height_task)
UI_PAGE(ui_set_imu_pitch_task)
UI_PAGE(ui_prompt_info_task)
UI_PAGE(ui_transcribe_info_task)

/* the panel's default task gets the screen id where the pages get the event */
static int default_task(struct jbd_panel_context *panel, int screen_id, void *ctx) {
  ui_log("default_task", screen_id, panel, ctx);
  return 0;
}

DEFINE_FAKE_VOID_FUNC(jbd_clear_cache_sync, uint8_t);
FAKE_VALUE_FUNC(int, set_frame_buffer, uint8_t **);
FAKE_VOID_FUNC(set_hide_icon_on_exit);

static void display_before(void *fixture) {
  ARG_UNUSED(fixture);

  k_msgq_purge(&display_msgq);
  display_wait_ms = 0x42;
  memset(&disp_state, 0, sizeof(disp_state));
  display_prio_boosted = 0;
  display_delay_close_ms = 0;
  last_ui_screen_id = 0;
  display_tid = NULL;
  ui_call_count = 0;
  page_result = 0;
  RESET_FAKE(jbd_clear_cache_sync);
  RESET_FAKE(set_hide_icon_on_exit);
  g1_state()->panel.default_task = default_task;
}

static void display_after(void *fixture) {
  ARG_UNUSED(fixture);

  if (display_tid) {
    k_thread_abort(display_tid);
    display_tid = NULL;
  }
  k_msgq_purge(&display_msgq);
}

ZTEST_SUITE(display_thread, NULL, NULL, display_before, display_after, NULL);

static struct GlassesState *display_start(void) {
  struct GlassesState *gs = g1_state();

  start_display_thread_handler(gs);
  g1_clock_advance_ms(1);
  return gs;
}

static struct GlassesState *display_start_running(uint8_t id) {
  struct GlassesState *gs = display_start();

  gs->sync.screen_id = id;
  display_reflash_screen(id);
  zassert_equal(disp_state.state, 1, "screen %d did not start", id);
  ui_call_count = 0;
  RESET_FAKE(jbd_clear_cache_sync);
  return gs;
}

static void expect_ui_call(int index, const char *page, int event) {
  zassert_true(index < ui_call_count, "call %d of %s(%d) missing (%d calls)", index, page, event, ui_call_count);
  zassert_true(strcmp(ui_calls[index].page, page) == 0 && ui_calls[index].event == event,
               "call %d: %s(%d), expected %s(%d)", index, ui_calls[index].page, ui_calls[index].event, page, event);
}

/* ---- open ---- */

ZTEST(display_thread, test_reflash_screen_from_idle_enters_that_screen_page) {
  struct GlassesState *gs = display_start();

  display_reflash_screen(6);

  zassert_equal(ui_call_count, 1);
  expect_ui_call(0, "ui_DashBoard_task", UI_EVT_ENTER);
  zassert_equal_ptr(ui_calls[0].panel, &gs->panel);
  zassert_equal_ptr(ui_calls[0].arg, &gs->sync);
  zassert_equal(last_ui_screen_id, 6);
  zassert_equal(disp_state.state, 1, "running");
  zassert_equal(disp_state.screen_id, 6);
}

ZTEST(display_thread, test_reflash_without_payload_enters_the_current_sync_screen) {
  struct GlassesState *gs = display_start();

  gs->sync.screen_id = 9; /* teleprompter */
  zassert_equal(display_reflash(NULL, 0), 0);
  g1_clock_advance_ms(1);

  zassert_equal(ui_call_count, 1);
  expect_ui_call(0, "ui_teleprompter_task", UI_EVT_ENTER);
  zassert_equal(disp_state.screen_id, 9);
}

/* display_reflash() payload {marker, screen id}: marker 1 marks the id valid */
static const uint8_t open_dashboard[2] = {1, 6};

ZTEST(display_thread, test_reflash_with_invalid_marker_byte_opens_nothing) {
  const uint8_t data[2] = {0, 6};

  display_start();
  zassert_equal(display_reflash(data, sizeof(data)), 0);
  g1_clock_advance_ms(200);

  zassert_equal(ui_call_count, 0, "%d page calls", ui_call_count);
  zassert_equal(disp_state.state, 0, "still idle");
}

ZTEST(display_thread, test_payload_longer_than_10_bytes_is_rejected) {
  uint8_t data[11] = {1, 6};

  zassert_equal(display_reflash(data, sizeof(data)), -1);
  zassert_equal(display_close(data, sizeof(data)), -1);
  zassert_equal(k_msgq_num_used_get(&display_msgq), 0);
}

/* ---- running ---- */

ZTEST(display_thread, test_running_screen_gets_refresh_every_display_wait_ms) {
  display_start_running(6);

  g1_clock_advance_ms(5 * 67 + 10);

  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_REFRESH), 5, "%d refreshes",
                count_ui_calls("ui_DashBoard_task", UI_EVT_REFRESH));
  zassert_equal(ui_call_count, 5);
  /* 0x42 = 66 ms; a relative timeout may end one tick later */
  for (int index = 1; index < ui_call_count; index++) {
    int64_t gap = ui_calls[index].uptime_ms - ui_calls[index - 1].uptime_ms;

    zassert_true(gap >= 66 && gap <= 67, "refresh %d after %lld ms", index, gap);
  }
}

ZTEST(display_thread, test_set_display_wait_ms_changes_the_refresh_period) {
  display_start_running(6);

  zassert_equal(set_display_wait_ms(100), 0);
  g1_clock_advance_ms(67);
  ui_call_count = 0;
  g1_clock_advance_ms(4 * 101 + 10);

  zassert_equal(ui_call_count, 4, "%d refreshes", ui_call_count);
  for (int index = 1; index < ui_call_count; index++) {
    int64_t gap = ui_calls[index].uptime_ms - ui_calls[index - 1].uptime_ms;

    zassert_true(gap >= 100 && gap <= 101, "refresh %d after %lld ms", index, gap);
  }
}

ZTEST(display_thread, test_set_display_wait_ms_clamps_to_33_and_125_ms) {
  set_display_wait_ms(10);
  zassert_equal(display_wait_ms, 33);
  set_display_wait_ms(1000);
  zassert_equal(display_wait_ms, 125);
  set_display_wait_ms(-5);
  zassert_equal(display_wait_ms, 33);
  set_display_wait_ms(0x42);
  zassert_equal(display_wait_ms, 0x42);
}

ZTEST(display_thread, test_input_master_close_and_power_messages_reach_the_running_page) {
  display_start_running(6);

  zassert_equal(display_inputEvent(1, 2), 0);
  zassert_equal(display_MasterSendClose(), 0);
  zassert_equal(display_powerEvent(1), 0);
  zassert_equal(display_powerEvent(0), 0);
  g1_clock_advance_ms(1);

  zassert_equal(ui_call_count, 4);
  expect_ui_call(0, "ui_DashBoard_task", UI_EVT_INPUT);
  expect_ui_call(1, "ui_DashBoard_task", UI_EVT_MASTER_CLOSE);
  expect_ui_call(2, "ui_DashBoard_task", UI_EVT_LOW_POWER);
  expect_ui_call(3, "ui_DashBoard_task", UI_EVT_POWER_RESUME);
  zassert_equal(disp_state.state, 1, "still running");
}

ZTEST(display_thread, test_queued_reflashes_are_each_handled_without_refresh_in_between) {
  display_start_running(6);

  zassert_equal(display_reflash(open_dashboard, sizeof(open_dashboard)), 0);
  zassert_equal(display_reflash(open_dashboard, sizeof(open_dashboard)), 0);
  zassert_equal(display_reflash(open_dashboard, sizeof(open_dashboard)), 0);
  g1_clock_advance_ms(1);

  zassert_equal(ui_call_count, 3, "%d page calls", ui_call_count);
  expect_ui_call(0, "ui_DashBoard_task", UI_EVT_ENTER);
  expect_ui_call(1, "ui_DashBoard_task", UI_EVT_ENTER);
  expect_ui_call(2, "ui_DashBoard_task", UI_EVT_ENTER);
}

ZTEST(display_thread, test_queue_holds_30_messages_and_rejects_the_31st) {
  for (int index = 0; index < 30; index++) {
    zassert_equal(display_inputEvent(index, 0), 0, "message %d", index);
  }
  zassert_equal(display_inputEvent(30, 0), -1);
  zassert_equal(display_DelayClose(100), -1);
  zassert_equal(display_powerEvent(1), -1);
  zassert_equal(k_msgq_num_used_get(&display_msgq), 30);
}

ZTEST(display_thread, test_opening_another_screen_exits_the_running_one_first) {
  struct GlassesState *gs = display_start_running(6);

  gs->sync.screen_id = 9;
  display_reflash_screen(9);

  zassert_equal(ui_call_count, 2);
  expect_ui_call(0, "ui_DashBoard_task", UI_EVT_EXIT);
  expect_ui_call(1, "ui_teleprompter_task", UI_EVT_ENTER);
  zassert_equal(disp_state.screen_id, 9);
  zassert_equal(disp_state.state, 1);
}

/* ---- close ---- */

ZTEST(display_thread, test_close_screen_exits_the_page_and_clears_the_panel) {
  struct GlassesState *gs = display_start_running(6);

  memset(gs->ui.buf, 0xa5, sizeof(gs->ui.buf));
  display_close_screen(6);

  zassert_equal(ui_call_count, 1);
  expect_ui_call(0, "ui_DashBoard_task", UI_EVT_EXIT);
  zassert_equal(jbd_clear_cache_sync_fake.call_count, 1);
  zassert_equal(jbd_clear_cache_sync_fake.arg0_val, 0);
  zassert_equal(disp_state.state, 0, "idle");
  expect_fill(gs->ui.buf, sizeof(gs->ui.buf), 0, "ui.buf");

  g1_clock_advance_ms(300);
  zassert_equal(ui_call_count, 1, "%d page calls", ui_call_count);
}

ZTEST(display_thread, test_closing_the_notification_screen_hides_its_icon) {
  display_start_running(4);

  display_close_screen(4);

  zassert_equal(set_hide_icon_on_exit_fake.call_count, 1);
  expect_ui_call(0, "ui_ancs_notificaton_task", UI_EVT_EXIT);
}

ZTEST(display_thread, test_close_screen_0_does_nothing) {
  display_start_running(6);

  display_close_screen(0);
  g1_clock_advance_ms(1);

  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_EXIT), 0);
  zassert_equal(disp_state.state, 1, "still running");
}

ZTEST(display_thread, test_close_while_idle_exits_the_named_page) {
  display_start();

  display_close_screen(9);

  zassert_equal(ui_call_count, 1);
  expect_ui_call(0, "ui_teleprompter_task", UI_EVT_EXIT);
  zassert_equal(disp_state.state, 0);
}

ZTEST(display_thread, test_closing_a_different_screen_id_exits_the_running_page) {
  display_start_running(6);

  display_close_screen(9);

  zassert_equal(disp_state.state, 0, "dispatcher idle");
  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_EXIT), 1, "running dashboard page got no EXIT");
}

ZTEST(display_thread, test_screen_id_0_while_running_closes_the_screen_without_a_page_call) {
  struct GlassesState *gs = display_start_running(6);

  gs->sync.screen_id = 0;
  g1_clock_advance_ms(70);

  zassert_equal(jbd_clear_cache_sync_fake.call_count, 1);
  zassert_equal(jbd_clear_cache_sync_fake.arg0_val, 0);
  zassert_equal(disp_state.state, 0);
  zassert_equal(ui_call_count, 0, "%d page calls", ui_call_count);

  g1_clock_advance_ms(300);
  zassert_equal(ui_call_count, 0);
  zassert_equal(jbd_clear_cache_sync_fake.call_count, 1);
}

/* ---- delay close ---- */

ZTEST(display_thread, test_delay_close_hands_the_delay_to_the_running_page) {
  display_start_running(12); /* quick note */

  zassert_equal(display_DelayClose(10000), 0);
  g1_clock_advance_ms(1);

  zassert_equal(ui_call_count, 1);
  expect_ui_call(0, "ui_QuickNote_task", UI_EVT_DELAY_CLOSE);
  zassert_equal(get_display_delay_close_ms(), 10000);
}

/* display_timer is never started: the page ends itself on REFRESH. */
ZTEST(display_thread, test_delay_close_leaves_closing_to_the_page_refreshes) {
  display_start_running(12);

  zassert_equal(display_DelayClose(500), 0);
  g1_clock_advance_ms(1000);

  expect_ui_call(0, "ui_QuickNote_task", UI_EVT_DELAY_CLOSE);
  zassert_true(count_ui_calls("ui_QuickNote_task", UI_EVT_REFRESH) >= 14, "%d refreshes",
               count_ui_calls("ui_QuickNote_task", UI_EVT_REFRESH));
  zassert_equal(count_ui_calls("ui_QuickNote_task", UI_EVT_EXIT), 0);
  zassert_equal(jbd_clear_cache_sync_fake.call_count, 0);
  zassert_equal(disp_state.state, 1, "still running");
  zassert_false(k_timer_remaining_get(&display_timer) > 0, "display_timer running");
}

ZTEST(display_thread, test_delay_close_while_idle_is_dropped) {
  display_start();

  zassert_equal(display_DelayClose(1000), 0);
  g1_clock_advance_ms(1);

  zassert_equal(ui_call_count, 0);
  zassert_equal(k_msgq_num_used_get(&display_msgq), 0, "message consumed");
  zassert_equal(get_display_delay_close_ms(), 0);
}

/* ---- DFU ---- */

/* work_mode 1 / 8: polls every 5 s */
ZTEST(display_thread, test_messages_wait_while_in_dfu_mode) {
  struct GlassesState *gs = g1_state();

  gs->work_mode = 1;
  display_start();
  display_reflash_screen(6);
  g1_clock_advance_ms(1000);

  zassert_equal(ui_call_count, 0);
  zassert_equal(k_msgq_num_used_get(&display_msgq), 1);

  gs->work_mode = 0;
  g1_clock_advance_ms(4100);
  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_ENTER), 1);
}

/* ---- priority ---- */

/* without the scheduler lock the ready thread runs at -16 before display_reflash() returns */
ZTEST(display_thread, test_reflash_boosts_the_thread_until_it_has_handled_the_request) {
  display_start();
  zassert_equal(k_thread_priority_get(display_tid), PRIO_NORMAL);

  display_reflash(open_dashboard, sizeof(open_dashboard));
  g1_clock_advance_ms(1);

  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_ENTER), 1);
  zassert_equal(k_thread_priority_get(display_tid), PRIO_NORMAL, "priority %d", k_thread_priority_get(display_tid));
}

/* the scheduler lock keeps the thread from running before the flag is set */
ZTEST(display_thread, test_boost_is_undone_after_an_open_from_idle) {
  display_start();

  k_sched_lock();
  display_reflash(open_dashboard, sizeof(open_dashboard));
  zassert_equal(k_thread_priority_get(display_tid), PRIO_BOOSTED);
  zassert_equal(display_prio_boosted, 1);
  k_sched_unlock();
  g1_clock_advance_ms(1);

  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_ENTER), 1);
  zassert_equal(k_thread_priority_get(display_tid), PRIO_NORMAL);
  zassert_equal(display_prio_boosted, 0);
}

ZTEST(display_thread, test_close_screen_boost_is_undone_after_the_close) {
  display_start_running(6);

  display_close_screen(6);

  zassert_equal(k_thread_priority_get(display_tid), PRIO_NORMAL);
  zassert_equal(display_prio_boosted, 0);
}

ZTEST(display_thread, test_restore_without_a_boost_leaves_the_priority_alone) {
  display_start();
  k_thread_priority_set(display_tid, -10);

  display_thread_restore_priority();

  zassert_equal(k_thread_priority_get(display_tid), -10);
}

ZTEST(display_thread, test_reflash_while_running_drops_the_boost_after_handling) {
  display_start_running(6);
  display_thread_restore_priority();
  zassert_equal(k_thread_priority_get(display_tid), PRIO_NORMAL);

  k_sched_lock();
  display_reflash(open_dashboard, sizeof(open_dashboard));
  k_sched_unlock();
  g1_clock_advance_ms(1);

  zassert_equal(count_ui_calls("ui_DashBoard_task", UI_EVT_ENTER), 1);
  zassert_equal(k_thread_priority_get(display_tid), PRIO_NORMAL, "priority %d", k_thread_priority_get(display_tid));
}

ZTEST(display_thread, test_opening_even_ai_text_clears_the_panel_cache_first) {
  struct GlassesState *gs = display_start();

  gs->sync.screen_id = 16;
  gs->dash_buf->type = 3;
  display_reflash(NULL, 0);
  g1_clock_advance_ms(1);

  zassert_equal(jbd_clear_cache_sync_fake.call_count, 1);
  expect_ui_call(0, "ui_even_ai_task", UI_EVT_ENTER);
}

/* ---- ui_task_handler ---- */

ZTEST(display_thread, test_ui_task_handler_routes_each_screen_id_to_its_page) {
  static const struct {
    int id;
    const char *page;
  } table[] = {
    {4, "ui_ancs_notificaton_task"}, {5, "ui_new_message_come_on_task"},
    {6, "ui_DashBoard_task"},        {7, "ui_bitmap_task"},
    {9, "ui_teleprompter_task"},     {10, "ui_navigation_task"},
    {11, "ui_translate_task"},       {12, "ui_QuickNote_task"},
    {14, "ui_onboarding_task"},      {15, "ui_raster_height_task"},
    {16, "ui_even_ai_task"},         {17, "ui_set_imu_pitch_task"},
    {18, "ui_prompt_info_task"},     {19, "ui_transcribe_info_task"},
    {20, "ui_even_ai_v2_info_task"},
  };
  struct GlassesState *gs = g1_state();

  for (size_t row = 0; row < ARRAY_SIZE(table); row++) {
    ui_call_count = 0;
    ui_task_handler(gs, table[row].id, &gs->sync, UI_EVT_INPUT);
    zassert_equal(ui_call_count, 1, "screen %d: %d calls", table[row].id, ui_call_count);
    expect_ui_call(0, table[row].page, UI_EVT_INPUT);
    zassert_equal_ptr(ui_calls[0].panel, &gs->panel, "screen %d", table[row].id);
    zassert_equal_ptr(ui_calls[0].arg, &gs->sync, "screen %d", table[row].id);
    zassert_equal(last_ui_screen_id, table[row].id);
  }
}

ZTEST(display_thread, test_screen_without_a_page_goes_to_the_panel_default_task) {
  struct GlassesState *gs = g1_state();
  static const int ids[] = {0, 1, 2, 8, 13};

  for (size_t row = 0; row < ARRAY_SIZE(ids); row++) {
    ui_call_count = 0;
    gs->sync.screen_id = ids[row];
    ui_task_handler(gs, ids[row], &gs->sync, UI_EVT_REFRESH);
    zassert_equal(ui_call_count, 1, "screen %d", ids[row]);
    expect_ui_call(0, "default_task", ids[row]);
    zassert_equal_ptr(ui_calls[0].panel, &gs->panel);
    zassert_equal_ptr(ui_calls[0].arg, &gs->sync);
  }
}

ZTEST(display_thread, test_ui_task_handler_returns_the_page_result) {
  struct GlassesState *gs = g1_state();

  page_result = 7;
  zassert_equal(ui_task_handler(gs, 9, &gs->sync, UI_EVT_REFRESH), 7);
  expect_ui_call(0, "ui_teleprompter_task", UI_EVT_REFRESH);
}

ZTEST(display_thread, test_pt_mode_runs_only_the_demo_image_screen) {
  struct GlassesState *gs = g1_state();

  get_pt_test_mode_fake.return_val = 1;

  zassert_equal(ui_task_handler(gs, 3, &gs->sync, UI_EVT_ENTER), 0);
  zassert_equal(ui_task_handler(gs, 6, &gs->sync, UI_EVT_ENTER), 0);

  zassert_equal(ui_call_count, 1);
  expect_ui_call(0, "ui_demo_image_task", UI_EVT_ENTER);
  zassert_equal_ptr(ui_calls[0].panel, &gs->panel);
  zassert_equal(last_ui_screen_id, 0, "not recorded in test mode");
}
