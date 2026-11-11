/* Task screens: teleprompter, navigation, translate, quicknote, bitmap, prompt, flag tasks. */
#include "screen_test.h"

static void teleprompter_setup(void)
{
  showing(9);
  persist_task(9, 2);
  gs->teleprompter->ready = 1;
}

ZTEST(screen_state, test_teleprompter_stays_while_ready)
{
  teleprompter_setup();
  expect_screen_stays(2000);
  zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

/* the reason string is misspelled in the source */
ZTEST(screen_state, test_teleprompter_stopped_by_app_closes_to_idle)
{
  teleprompter_setup();

  start_screen_expect_running(200);
  gs->teleprompter->ready = 0;

  expect_task_closed(SELF_EXIT_MS, "temeprompter:self exit");
  zassert_true(done_after() >= 200 + 500);
  zassert_equal(dmic_stream_stop_fake.call_count, 1);
  zassert_equal(teleprompter_state_reset_fake.call_count, 1);
  zassert_equal(gs->task_status->status, 1);
}

ZTEST(screen_state, test_teleprompter_stopped_with_head_up_wakes_for_dashboard)
{
  teleprompter_setup();
  gs->screen_on = 0;
  gs->teleprompter->ready = 0;
  head_up();

  screen_start();

  expect_task_closed(501 + 301 + 5, "temeprompter:self exit");
  zassert_true(done_after() >= 800, "exit after %d ms", (int)done_after());
  expect_str(triggers(), "IMU:wakeup:dashboard->1");
  zassert_equal(teleprompter_state_reset_fake.call_count, 1);
}

ZTEST(screen_state, test_teleprompter_closes_on_dfu)
{
  teleprompter_setup();
  expect_dfu_exit(8, "teleprompter:imu-trigger-off-imm");
  expect_str(syncs(), "");
}

static void navigation_setup(void)
{
  showing(10);
  persist_task(10, 2);
  gs->nav->running = 1;
}

ZTEST(screen_state, test_navigation_stays_while_running)
{
  navigation_setup();
  expect_screen_stays(2000);
}

ZTEST(screen_state, test_navigation_exit_closes_to_idle)
{
  navigation_setup();

  start_screen_expect_running(200);
  gs->nav->running = 0;

  expect_task_closed(SELF_EXIT_MS, "navigation:self exit");
  expect_str(triggers(), "");
}

ZTEST(screen_state, test_navigation_exit_with_head_up_wakes_for_dashboard)
{
  navigation_setup();
  gs->screen_on = 0;
  gs->nav->running = 0;
  head_up();

  screen_start();

  expect_exit_within(501 + 301 + 5, RET_TASK_EXITED, "navigation:self exit");
  zassert_true(done_after() >= 800);
  expect_str(triggers(), "IMU:wakeup:dashboard->1");
  zassert_equal(gs->dashboard_mode, 0);
}

static void translate_setup(void)
{
  showing(11);
  persist_task(11, 2);
  gs->translate_src->data[0] = 'h';
  gs->translate_dst->data[0] = 'b';
}

ZTEST(screen_state, test_translate_stays_while_one_text_present)
{
  translate_setup();
  gs->translate_src->data[0] = 0;
  expect_screen_stays(2000);
}

ZTEST(screen_state, test_translate_both_texts_cleared_closes_to_idle)
{
  translate_setup();

  start_screen_expect_running(200);
  gs->translate_src->data[0] = 0;
  gs->translate_dst->data[0] = 0;

  expect_task_closed(SELF_EXIT_MS, "translate:self exit");
}

/* no screen wake, unlike teleprompter / navigation */
ZTEST(screen_state, test_translate_cleared_with_head_up_closes_after_800ms)
{
  translate_setup();
  gs->translate_src->data[0] = 0;
  gs->translate_dst->data[0] = 0;
  head_up();

  screen_start();

  expect_exit_within(501 + 301 + 5, RET_TASK_EXITED, "translate:self exit");
  zassert_true(done_after() >= 800);
  expect_str(triggers(), "");
}

ZTEST(screen_state, test_translate_closes_on_dfu)
{
  translate_setup();
  expect_dfu_exit(8, "translate:self exit");
  expect_str(syncs(), "");
}

static void quicknote_setup(void)
{
  showing(12);
  persist_task(12, 2);
  gs->quicknote_show_info->enable = 1;
}

ZTEST(screen_state, test_quicknote_stays_while_enabled)
{
  quicknote_setup();
  expect_screen_stays(2000);
}

ZTEST(screen_state, test_quicknote_disabled_head_down_closes_to_idle)
{
  quicknote_setup();

  start_screen_expect_running(200);
  gs->quicknote_show_info->enable = 0;

  expect_task_closed(SELF_EXIT_MS, "quicknote:self exit");
}

ZTEST(screen_state, test_quicknote_disabled_head_up_waits_for_head_down)
{
  quicknote_setup();
  gs->screen_on = 0;
  gs->quicknote_show_info->enable = 0;
  head_up();

  screen_start();

  expect_exit_within(501 + 5, RET_TASK_EXITED, "quicknote:self exit");
  expect_str(syncs(), "08");
  zassert_equal(gs->dashboard_mode, 1);
  zassert_equal(gs->task_status->task, 1);
  expect_str(triggers(), "IMU:wakeup:dashboard->1");
}

static void bitmap_setup(void)
{
  showing(7);
  persist_task(7, 2);
  gs->bitmap_info->enable = 1;
  gs->bitmap_info->deadline = k_uptime_get();
  gs->bitmap_info->sync_pending = 1;
}

ZTEST(screen_state, test_not_disturb_entry_clears_sync_pending)
{
  bitmap_setup();

  start_screen_expect_running(100);

  zassert_equal(gs->bitmap_info->sync_pending, 0);
}

/* no sync 8: the bitmap is lens-local */
ZTEST(screen_state, test_not_disturb_closes_1500ms_after_deadline)
{
  bitmap_setup();

  start_screen_expect_running(1500);
  zassert_equal(gs->bitmap_info->enable, 1);

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "not_disturb:self exit");
  zassert_equal(gs->bitmap_info->enable, 0);
  zassert_equal(gs->dashboard_mode, 0);
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_not_disturb_disabled_closes_to_idle)
{
  bitmap_setup();
  gs->bitmap_info->deadline = k_uptime_get() + 100000;

  start_screen_expect_running(300);
  gs->bitmap_info->enable = 0;

  expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "not_disturb:self exit");
  zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_not_disturb_wait_blow_head_request_ends_screen)
{
  bitmap_setup();
  gs->bitmap_info->deadline = k_uptime_get() + 100000;
  expect_wait_blow_head_exit("not_disturb:imu-trigger-off-imm");
}

static void prompt_setup(void)
{
  showing(18);
  persist_task(18, 2);
  gs->ts_info->enable = 1;
  gs->ts_info->ts = k_uptime_get();
}

ZTEST(screen_state, test_prompt_master_times_out_4s_after_page_entry)
{
  prompt_setup();

  start_screen_expect_running(4000);
  zassert_equal(gs->ts_info->enable, 1);

  expect_task_closed(SELF_EXIT_MS, "prompt:self exit");
  zassert_equal(gs->ts_info->enable, 0);
}

ZTEST(screen_state, test_prompt_slave_does_not_time_out)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  prompt_setup();

  start_screen_expect_running(6000);

  zassert_equal(gs->ts_info->enable, 1);
}

ZTEST(screen_state, test_prompt_disabled_closes_to_idle)
{
  prompt_setup();

  start_screen_expect_running(300);
  gs->ts_info->enable = 0;

  expect_exit_within(SELF_EXIT_MS, RET_TASK_EXITED, "prompt:self exit");
}

/* the task screen set up by setup ends at once on a DFU, with reason */
#define DFU_TEST(name, setup, work_mode, reason)                                                   \
  ZTEST(screen_state, test_##name)                                                           \
  {                                                                                          \
    setup();                                                                           \
    expect_dfu_exit(work_mode, reason);                                                \
  }

DFU_TEST(navigation_closes_on_dfu, navigation_setup, 1, "navigation:imu-trigger-off-imm")
DFU_TEST(quicknote_closes_on_dfu, quicknote_setup, 1, "quicknote:imu-trigger-off-imm")
DFU_TEST(not_disturb_closes_on_dfu, bitmap_setup, 1, "not_disturb:dfu")
DFU_TEST(prompt_closes_on_dfu, prompt_setup, 8, "prompt:dfu")

/* the task screen set up by setup ends on a wait-for-head-down request, with reason */
#define WAIT_BLOW_HEAD_TEST(name, setup, reason)                                                   \
  ZTEST(screen_state, test_##name)                                                           \
  {                                                                                          \
    setup();                                                                           \
    expect_wait_blow_head_exit(reason);                                                \
  }

WAIT_BLOW_HEAD_TEST(teleprompter_wait_blow_head_request_ends_screen, teleprompter_setup,
        "dashboard:imu-trigger-off-imm")
WAIT_BLOW_HEAD_TEST(navigation_wait_blow_head_request_ends_screen, navigation_setup,
        "dashboard:imu-trigger-off-imm")
WAIT_BLOW_HEAD_TEST(translate_wait_blow_head_request_ends_screen, translate_setup,
        "dashboard:imu-trigger-off-imm")
WAIT_BLOW_HEAD_TEST(quicknote_wait_blow_head_request_ends_screen, quicknote_setup,
        "dashboard:imu-trigger-off-imm")
WAIT_BLOW_HEAD_TEST(prompt_wait_blow_head_request_ends_screen, prompt_setup,
        "prompt:imu-trigger-off-imm")

/* onboarding (14), raster height (15), IMU pitch (17) and transcribe (19) run until a flag drops */
static uint8_t *task_flag(int screen)
{
  switch (screen) {
  case 14:
    return &gs->onboarding->active;
  case 15:
    return &gs->raster_height_cfg[0];
  case 17:
    return &gs->horiz_pitch_cfg[0];
  default:
    return (uint8_t *)&gs->transcribe_buf->running;
  }
}

static void flag_task_setup(int screen)
{
  showing(screen);
  *task_flag(screen) = 1;
}

/* the task runs while its flag is set; once it drops, the master closes it after its delay */
static void expect_flag_cleared_closes(int screen, int master_delay_ms, const char *reason)
{
  flag_task_setup(screen);
  persist_task(screen, 2);
  expect_screen_stays(500);

  *task_flag(screen) = 0;
  expect_task_closed(POLL_MS + master_delay_ms + 1 + 5, reason);
  zassert_true(done_after() >= 500 + master_delay_ms, "exit after %d ms", (int)done_after());
  zassert_equal(gs->task_status->status, 1);
}

ZTEST(screen_state, test_onboarding_inactive_closes_to_idle_after_100ms)
{
  expect_flag_cleared_closes(14, 100, "onboarding:self exit");
}

ZTEST(screen_state, test_onboarding_closes_on_dfu)
{
  flag_task_setup(14);
  expect_dfu_exit(1, "onboarding:dfu");
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_onboarding_wait_blow_head_request_ends_screen)
{
  flag_task_setup(14);
  expect_wait_blow_head_exit("onboarding:imu-trigger-off-imm");
}

ZTEST(screen_state, test_raster_height_done_closes_to_idle_after_500ms)
{
  expect_flag_cleared_closes(15, 500, "raster_height:self exit");
}

ZTEST(screen_state, test_raster_height_closes_on_dfu)
{
  flag_task_setup(15);
  expect_dfu_exit(1, "raster_height:dfu");
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_raster_height_wait_blow_head_request_ends_screen)
{
  flag_task_setup(15);
  expect_wait_blow_head_exit("raster_height:imu-trigger-off-imm");
}

ZTEST(screen_state, test_set_imu_pitch_done_closes_to_idle_after_500ms)
{
  expect_flag_cleared_closes(17, 500, "set_imu_pitch:self exit");
}

ZTEST(screen_state, test_set_imu_pitch_closes_on_dfu)
{
  flag_task_setup(17);
  expect_dfu_exit(1, "set_imu_pitch:dfu");
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_set_imu_pitch_wait_blow_head_request_ends_screen)
{
  flag_task_setup(17);
  expect_wait_blow_head_exit("set_imu_pitch:imu-trigger-off-imm");
}

ZTEST(screen_state, test_transcribe_stopped_closes_to_idle_after_500ms)
{
  expect_flag_cleared_closes(19, 500, "transcribe:self exit");
}

ZTEST(screen_state, test_transcribe_closes_on_dfu)
{
  flag_task_setup(19);
  expect_dfu_exit(1, "transcribe:dfu");
  expect_str(syncs(), "");
}

ZTEST(screen_state, test_transcribe_wait_blow_head_request_ends_screen)
{
  flag_task_setup(19);
  expect_wait_blow_head_exit("transcribe:imu-trigger-off-imm");
}

ZTEST(screen_state, test_transcribe_slave_stopped_closes_without_delay)
{
  g1_unit_set_role(G1_ROLE_SLAVE);
  showing(19);
  gs->transcribe_buf->running = 0;

  screen_start();

  expect_exit_within(10, RET_TASK_EXITED, "transcribe:self exit");
}
