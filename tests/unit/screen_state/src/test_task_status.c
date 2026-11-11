/*
 * task_status.c and system_state.c.
 * Status: 1 stopped, 2 running, 3 paused. Screens: 0 idle, 1 wait for head-down, 4 message, 6
 * dashboard, 7 bitmap, 16 even AI, 20 even AI v2.
 * suspend_state: 0 running, 1 suspended, 2 suspending, 3 resuming.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_screen.h"
#include "threads/threads.h"

static struct GlassesState *st(void)
{
	return g1_state();
}

static void record(int task, int status)
{
	st()->task_status->task = task;
	st()->task_status->status = status;
}

#define expect_record(t, s)                                                                        \
	do {                                                                                       \
		zassert_equal(st()->task_status->task, (t), "task %u",                             \
			      st()->task_status->task);                                            \
		zassert_equal(st()->task_status->status, (s), "status %u",                         \
			      st()->task_status->status);                                          \
	} while (0)

ZTEST(screen_state, test_persist_task_active_when_running_or_paused)
{
	record(9, 0);
	zassert_equal(is_persist_task_active(st(), 0), 0);
	record(9, 1);
	zassert_equal(is_persist_task_active(st(), 0), 0);
	record(9, 2);
	zassert_equal(is_persist_task_active(st(), 0), 1);
	record(9, 3);
	zassert_equal(is_persist_task_active(st(), 0), 1);
}

ZTEST(screen_state, test_task_running_means_screen_above_1)
{
	st()->dashboard_mode = 0;
	zassert_equal(is_task_running(st()), 0);
	st()->dashboard_mode = 1;
	zassert_equal(is_task_running(st()), 0);
	st()->dashboard_mode = 2;
	zassert_equal(is_task_running(st()), 1);
	st()->dashboard_mode = 20;
	zassert_equal(is_task_running(st()), 1);
}

ZTEST(screen_state, test_persist_update_from_idle_sets_screen_and_record)
{
	st()->dashboard_mode = 0;

	zassert_equal(update_persist_task_status(st(), 10, 2), 2);

	zassert_equal(st()->dashboard_mode, 10);
	expect_record(10, 2);
	zassert_equal(display_close_screen_fake.call_count, 0, "screen 0 has nothing to close");
}

ZTEST(screen_state, test_persist_update_from_wait_blow_head_sets_screen)
{
	st()->dashboard_mode = 1;

	update_persist_task_status(st(), 9, 2);

	zassert_equal(st()->dashboard_mode, 9);
	expect_record(9, 2);
	zassert_equal(display_close_screen_fake.call_count, 0);
}

ZTEST(screen_state, test_persist_update_same_task_same_status_is_ignored)
{
	st()->dashboard_mode = 9;
	record(9, 2);

	zassert_equal(update_persist_task_status(st(), 9, 2), 2);

	zassert_equal(display_close_screen_fake.call_count, 0);
	zassert_equal(st()->dashboard_mode, 9);
	expect_record(9, 2);
}

ZTEST(screen_state, test_persist_update_pause_of_stopped_task_is_ignored)
{
	st()->dashboard_mode = 6;
	record(2, 1);

	zassert_equal(update_persist_task_status(st(), 2, 3), 3);

	zassert_equal(display_close_screen_fake.call_count, 0);
	zassert_equal(st()->dashboard_mode, 6);
	expect_record(2, 1);
}

ZTEST(screen_state, test_persist_update_running_overlay_task_replaces_screen)
{
	static const uint8_t tasks[] = {4, 6, 7, 16, 20};

	for (size_t i = 0; i < ARRAY_SIZE(tasks); i++) {
		RESET_FAKE(display_close_screen);
		st()->dashboard_mode = 10;
		record(10, 2);

		update_persist_task_status(st(), tasks[i], 2);

		zassert_equal(display_close_screen_fake.call_count, 1, "task %d", tasks[i]);
		zassert_equal(display_close_screen_fake.arg0_val, 10);
		zassert_equal(st()->dashboard_mode, tasks[i]);
		expect_record(tasks[i], 2);
	}
}

ZTEST(screen_state, test_persist_update_running_task_over_other_screen_is_refused)
{
	st()->dashboard_mode = 10;
	record(10, 2);

	zassert_equal(update_persist_task_status(st(), 9, 2), 2);

	zassert_equal(display_close_screen_fake.call_count, 0);
	zassert_equal(st()->dashboard_mode, 10);
	expect_record(10, 2);
	zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_persist_update_pause_over_screen_switches_to_task)
{
	st()->dashboard_mode = 10;
	record(2, 2);

	zassert_equal(update_persist_task_status(st(), 2, 3), 3);

	zassert_equal(display_close_screen_fake.arg0_val, 10);
	zassert_equal(st()->dashboard_mode, 2);
	expect_record(2, 3);
}

ZTEST(screen_state, test_persist_update_during_dashboard_exit_records_only)
{
	st()->dashboard_mode = 6;
	st()->dashboard_ts->exit_pending = 1;
	record(0, 1);

	update_persist_task_status(st(), 9, 2);

	zassert_equal(st()->dashboard_mode, 6);
	zassert_equal(display_close_screen_fake.call_count, 0);
	expect_record(9, 2);
}

ZTEST(screen_state, test_persist_update_over_dashboard_without_exit_request_is_refused)
{
	st()->dashboard_mode = 6;
	record(0, 1);

	update_persist_task_status(st(), 9, 2);

	zassert_equal(st()->dashboard_mode, 6);
	expect_record(0, 1);
}

ZTEST(screen_state, test_persist_update_even_ai_suspends_auto_brightness_until_idle)
{
	st()->dashboard_mode = 0;
	st()->opt3007_context.auto_brightness = 1;

	update_persist_task_status(st(), 16, 2);
	zassert_equal(st()->opt3007_context.auto_brightness, 0);

	update_persist_task_status_to_idle(st());
	zassert_equal(st()->opt3007_context.auto_brightness, 1);
}

ZTEST(screen_state, test_persist_update_even_ai_keeps_manual_brightness)
{
	st()->dashboard_mode = 0;
	st()->opt3007_context.auto_brightness = 0;

	update_persist_task_status(st(), 16, 2);
	zassert_equal(st()->opt3007_context.auto_brightness, 0);

	update_persist_task_status_to_idle(st());
	zassert_equal(st()->opt3007_context.auto_brightness, 0);
}

ZTEST(screen_state, test_persist_update_running_resets_even_ai_v2_state)
{
	st()->dashboard_mode = 0;
	st()->evenai_buf->state = 2;

	update_persist_task_status(st(), 10, 2);
	zassert_equal(st()->evenai_buf->state, 0);

	st()->evenai_buf->state = 1;
	update_persist_task_status(st(), 11, 2);
	zassert_equal(st()->evenai_buf->state, 1, "state 1 is kept");
}

ZTEST(screen_state, test_temp_update_from_idle_sets_screen_and_clears_record)
{
	st()->dashboard_mode = 0;
	record(9, 1);

	zassert_equal(update_temp_task_status(st(), 6, 2), 2);

	zassert_equal(st()->dashboard_mode, 6);
	expect_record(0, 1);
	zassert_equal(display_close_screen_fake.call_count, 0);
}

ZTEST(screen_state, test_temp_update_message_over_screen_closes_old_screen)
{
	st()->dashboard_mode = 2;
	record(2, 2);

	update_temp_task_status(st(), 4, 2);

	zassert_equal(display_close_screen_fake.arg0_val, 2);
	zassert_equal(st()->dashboard_mode, 4);
	expect_record(0, 1);
}

ZTEST(screen_state, test_temp_update_other_task_over_screen_is_refused)
{
	st()->dashboard_mode = 5;
	record(0, 1);

	zassert_equal(update_temp_task_status(st(), 8, 2), 2);

	zassert_equal(st()->dashboard_mode, 5);
	zassert_equal(display_close_screen_fake.call_count, 0);
	zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_temp_update_current_screen_is_ignored)
{
	st()->dashboard_mode = 6;
	record(9, 2);

	update_temp_task_status(st(), 6, 2);

	zassert_equal(display_close_screen_fake.call_count, 0);
	expect_record(9, 2);
}

ZTEST(screen_state, test_temp_update_stop_over_screen_switches)
{
	st()->dashboard_mode = 9;
	record(9, 2);

	update_temp_task_status(st(), 5, 1);

	zassert_equal(display_close_screen_fake.arg0_val, 9);
	zassert_equal(st()->dashboard_mode, 5);
	expect_record(0, 1);
}

ZTEST(screen_state, test_to_idle_closes_screen_and_stops_record)
{
	st()->dashboard_mode = 10;
	record(10, 2);

	zassert_equal(update_persist_task_status_to_idle(st()), 0);

	zassert_equal(display_close_screen_fake.arg0_val, 10);
	zassert_equal(st()->dashboard_mode, 0);
	expect_record(10, 1);
}

ZTEST(screen_state, test_to_idle_when_idle_does_nothing)
{
	st()->dashboard_mode = 0;
	record(0, 1);

	update_persist_task_status_to_idle(st());

	zassert_equal(display_close_screen_fake.call_count, 0);
	expect_record(0, 1);
}

ZTEST(screen_state, test_to_idle_from_wait_blow_head_closes_nothing)
{
	st()->dashboard_mode = 1;
	record(1, 1);

	update_persist_task_status_to_idle(st());

	zassert_equal(display_close_screen_fake.call_count, 0);
	zassert_equal(st()->dashboard_mode, 0);
}

ZTEST(screen_state, test_to_wait_blow_head_closes_screen_and_records_1)
{
	st()->dashboard_mode = 12;
	record(12, 2);

	zassert_equal(update_persist_task_status_to_wait_blow_head(st()), 0);

	zassert_equal(display_close_screen_fake.arg0_val, 12);
	zassert_equal(st()->dashboard_mode, 1);
	expect_record(1, 1);
}

/* 9 teleprompter, 11 translate, 12 quicknote, 19 transcribe */
ZTEST(screen_state, test_task_uses_mic_for_voice_tasks_only)
{
	for (int t = 0; t <= 25; t++) {
		bool exp = t == 9 || t == 11 || t == 12 || t == 16 || t == 19 || t == 20;

		zassert_equal(task_uses_mic(t), exp, "task %d", t);
	}
}

ZTEST(screen_state, test_is_idle_needs_stopped_record_screen_0_and_suspend)
{
	record(0, 1);
	st()->dashboard_mode = 0;
	st()->suspend_state = 1;
	zassert_true(is_idle());

	st()->suspend_state = 0;
	zassert_false(is_idle(), "not suspended");
	st()->suspend_state = 1;

	record(9, 1);
	zassert_false(is_idle(), "task id kept");
	record(0, 2);
	zassert_false(is_idle(), "task running");
	record(0, 1);

	st()->dashboard_mode = 1;
	zassert_false(is_idle(), "screen 1");
}

static struct k_timer unlock_timer;

static void unlock_expiry(struct k_timer *t)
{
	ARG_UNUSED(t);
	task_status_lock = 0;
}

ZTEST(screen_state, test_task_status_lock_makes_update_wait)
{
	int64_t t0;

	k_timer_init(&unlock_timer, unlock_expiry, NULL);
	st()->dashboard_mode = 0;
	task_status_lock = 1;
	t0 = k_uptime_get();
	k_timer_start(&unlock_timer, K_MSEC(20), K_NO_WAIT);

	update_persist_task_status(st(), 10, 2);

	zassert_true(k_uptime_get() - t0 >= 20, "returned after %d ms",
		     (int)(k_uptime_get() - t0));
	zassert_equal(st()->dashboard_mode, 10);
	zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_task_status_lock_makes_readers_wait)
{
	int64_t t0;

	k_timer_init(&unlock_timer, unlock_expiry, NULL);
	record(9, 2);
	task_status_lock = 1;
	t0 = k_uptime_get();
	k_timer_start(&unlock_timer, K_MSEC(15), K_NO_WAIT);

	zassert_equal(is_persist_task_active(st(), 0), 1);
	zassert_true(k_uptime_get() - t0 >= 15);
}

ZTEST(screen_state, test_task_status_lock_released_on_every_path)
{
	st()->dashboard_mode = 9;
	record(9, 2);
	update_persist_task_status(st(), 9, 2); /* unchanged */
	zassert_equal(task_status_lock, 0);
	update_persist_task_status(st(), 10, 2); /* refused */
	zassert_equal(task_status_lock, 0);
	update_temp_task_status(st(), 9, 2); /* current screen */
	zassert_equal(task_status_lock, 0);
	update_temp_task_status(st(), 8, 2); /* refused */
	zassert_equal(task_status_lock, 0);
	update_persist_task_status_to_idle(st());
	zassert_equal(task_status_lock, 0);
	update_persist_task_status_to_idle(st()); /* already idle */
	zassert_equal(task_status_lock, 0);
	update_persist_task_status_to_wait_blow_head(st());
	zassert_equal(task_status_lock, 0);
}

ZTEST(screen_state, test_screen_on_sets_flag_and_gives_semaphore)
{
	zassert_equal(trigger_screen_state_change("t", st(), 1), 1);
	zassert_equal(st()->screen_on, 1);
	zassert_equal(k_sem_count_get(&st()->screen_on_sem), 1);

	zassert_equal(trigger_screen_state_change("t", st(), 0), 0);
	zassert_equal(st()->screen_on, 0);
}

ZTEST(screen_state, test_screen_on_repeated_is_ignored_without_counting)
{
	trigger_screen_state_change("a", st(), 1);
	k_sem_take(&st()->screen_on_sem, K_NO_WAIT);

	zassert_equal(trigger_screen_state_change("b", st(), 1), -1);
	zassert_equal(k_sem_count_get(&st()->screen_on_sem), 0);

	zassert_equal(trigger_screen_state_change("c", st(), 0), 0);
	zassert_equal(st()->screen_on, 0);
	zassert_equal(trigger_screen_state_change("d", st(), 0), -1);
}

ZTEST(screen_state, test_imu_on_in_normal_mode_wakes_imu_thread)
{
	st()->display_mode = 10;

	zassert_equal(set_imu_on_state(NULL, st(), 1), 0);
	zassert_equal(st()->imu.on, 1);
	zassert_equal(st()->imu_event, 1);
	zassert_equal(k_sem_count_get(&st()->imu_sem), 1);

	zassert_equal(set_imu_on_state(NULL, st(), 1), -1);
}

ZTEST(screen_state, test_imu_on_while_imu_busy_or_other_mode_does_not_wake)
{
	st()->display_mode = 10;
	st()->imu_busy = 1;
	set_imu_on_state(NULL, st(), 1);
	zassert_equal(st()->imu.on, 1);
	zassert_equal(st()->imu_event, 0);
	zassert_equal(k_sem_count_get(&st()->imu_sem), 0);

	set_imu_on_state(NULL, st(), 0);
	st()->imu_busy = 0;
	st()->display_mode = 12;
	set_imu_on_state(NULL, st(), 1);
	zassert_equal(st()->imu_event, 0);
}

ZTEST(screen_state, test_imu_state_change_switches_imu_and_screen)
{
	st()->display_mode = 10;

	zassert_equal(trigger_imu_state_change(NULL, st(), true), 0);
	zassert_equal(st()->imu.on, 1);
	zassert_equal(st()->screen_on, 1);

	zassert_equal(trigger_imu_state_change(NULL, st(), false), 0);
	zassert_equal(st()->imu.on, 0);
	zassert_equal(st()->screen_on, 0);

	zassert_equal(g1_fake_vlog_count(), 2);
	zassert_not_null(strstr(g1_fake_vlog_line(0), "imu-on: trigger_on_screen -> 1"));
	zassert_not_null(strstr(g1_fake_vlog_line(1), "imu-off: trigger_on_screen -> 0"));
}

ZTEST(screen_state, test_imu_state_change_repeated_on_is_not_counted)
{
	st()->display_mode = 10;
	trigger_imu_state_change(NULL, st(), true);
	st()->imu_event = 0;
	k_sem_take(&st()->imu_sem, K_NO_WAIT);

	trigger_imu_state_change(NULL, st(), true);
	zassert_equal(st()->imu_event, 0);
	zassert_equal(k_sem_count_get(&st()->imu_sem), 0);

	trigger_imu_state_change(NULL, st(), false);
	zassert_equal(st()->imu.on, 0);
	zassert_equal(st()->screen_on, 0);
}

ZTEST(screen_state, test_suspend_waits_300ms_then_suspends_panel)
{
	int64_t t0 = k_uptime_get();

	st()->suspend_state = 0;

	zassert_equal(global_system_suspend(st()), 0);

	zassert_equal(st()->suspend_state, 1);
	zassert_equal(g1_fake_panel_call_count, 1);
	zassert_equal(g1_fake_panel_calls[0].op, 'S');
	zassert_equal(g1_fake_panel_calls[0].suspend_state, 2);
	zassert_true(g1_fake_panel_calls[0].uptime_ms - t0 >= 300, "after %d ms",
		     (int)(g1_fake_panel_calls[0].uptime_ms - t0));
	zassert_equal(g1_fake_panel_resume_fake.call_count, 0);
}

/* bit 2 is a flag */
ZTEST(screen_state, test_suspend_from_state_4)
{
	st()->suspend_state = 4;

	global_system_suspend(st());

	zassert_equal(st()->suspend_state, 1);
	zassert_equal(g1_fake_panel_suspend_fake.call_count, 1);
}

ZTEST(screen_state, test_suspend_when_not_running_does_nothing)
{
	for (uint8_t s = 1; s <= 3; s++) {
		st()->suspend_state = s;
		global_system_suspend(st());
		zassert_equal(st()->suspend_state, s);
	}
	zassert_equal(g1_fake_panel_suspend_fake.call_count, 0);
}

ZTEST(screen_state, test_resume_resumes_panel_then_runs)
{
	int64_t t0 = k_uptime_get();

	st()->suspend_state = 1;

	zassert_equal(global_system_resume(st()), 0);

	zassert_equal(st()->suspend_state, 0);
	zassert_equal(g1_fake_panel_call_count, 1);
	zassert_equal(g1_fake_panel_calls[0].op, 'R');
	zassert_equal(g1_fake_panel_calls[0].suspend_state, 3);
	zassert_equal(g1_fake_panel_calls[0].uptime_ms, t0);
}

ZTEST(screen_state, test_resume_from_state_5)
{
	st()->suspend_state = 5;

	global_system_resume(st());

	zassert_equal(st()->suspend_state, 0);
	zassert_equal(g1_fake_panel_resume_fake.call_count, 1);
}

ZTEST(screen_state, test_resume_when_not_suspended_does_nothing)
{
	static const uint8_t states[] = {0, 2, 3, 4};

	for (size_t i = 0; i < ARRAY_SIZE(states); i++) {
		st()->suspend_state = states[i];
		global_system_resume(st());
		zassert_equal(st()->suspend_state, states[i]);
	}
	zassert_equal(g1_fake_panel_resume_fake.call_count, 0);
}
