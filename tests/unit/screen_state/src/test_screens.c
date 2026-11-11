/*
 * message_task.c screen handlers. Each runs in a worker thread (screen_start()) while the test
 * thread advances simulated time and changes inputs between its 50 ms polls.
 * Screens: 0 idle, 1 wait for head-down, 2 countdown, 4 message, 5 new message, 6 dashboard, 7
 * bitmap, 8 unread message, 9 teleprompter, 10 navigation, 11 translate, 12 quicknote, 14
 * onboarding, 15 raster height, 16 even AI, 17 IMU pitch, 18 prompt, 19 transcribe, 20 even AI v2.
 * imu.mode 2 head up, 1 head down; display_mode 10 normal, 12 silent; work_mode 1 / 8 DFU. Sync 8
 * closes the slave's screen; the master waits 500 ms before a self exit.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_misc.h"
#include "fakes_screen.h"

#include "threads/message_task.c"

#define RET_SCREEN_CHANGED 0x0b
#define RET_TASK_EXITED    (-1)
#define RET_NOT_DONE       0x7fff

/* Zephyr adds a tick to each relative sleep, so a 50 ms poll takes 51 ms. */
#define POLL_MS 51

typedef int (*screen_fn)(struct GlassesState *gs, struct imu_ctx *ctx, const char **reason);

#define SCREEN_STACK_SIZE 8192
static K_THREAD_STACK_DEFINE(screen_stack, SCREEN_STACK_SIZE);
static struct k_thread screen_thread;
static bool screen_alive;
static volatile bool screen_done;
static int screen_ret;
static const char *screen_reason;
static int64_t screen_t0;
static int64_t screen_done_ms;

static struct GlassesState *gs;

static void screen_entry(void *p1, void *p2, void *p3)
{
	screen_fn fn = (screen_fn)p1;
	const char *reason = NULL;
	int ret;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	ret = fn(g1_state(), &g1_state()->imu, &reason);
	screen_ret = ret;
	screen_reason = reason;
	screen_done_ms = k_uptime_get();
	screen_done = true;
}

/* the worker starts at the next test sleep */
static void screen_start(screen_fn fn)
{
	zassert_false(screen_alive && !screen_done, "a screen handler is already running");
	screen_done = false;
	screen_ret = RET_NOT_DONE;
	screen_reason = NULL;
	screen_t0 = k_uptime_get();
	k_thread_create(&screen_thread, screen_stack, K_THREAD_STACK_SIZEOF(screen_stack),
			screen_entry, (void *)fn, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	screen_alive = true;
}

static bool screen_wait(uint32_t max_ms)
{
	for (uint32_t t = 0; t < max_ms && !screen_done; t++) {
		g1_clock_advance_ms(1);
	}
	return screen_done;
}

static int64_t since_start(void)
{
	return k_uptime_get() - screen_t0;
}

static int64_t done_after(void)
{
	return screen_done_ms - screen_t0;
}

static void screen_stop(void)
{
	if (screen_alive) {
		if (!screen_done) {
			k_thread_abort(&screen_thread);
		} else {
			k_thread_join(&screen_thread, K_FOREVER);
		}
		screen_alive = false;
	}
}

static int run_not_disturb(struct GlassesState *s, struct imu_ctx *ctx, const char **reason)
{
	ARG_UNUSED(ctx);
	return process_for_not_disturb(s, reason);
}

static int run_dispatch(struct GlassesState *s, struct imu_ctx *ctx, const char **reason)
{
	ARG_UNUSED(reason);
	return screen_task_dispatch(s, ctx);
}

#define expect_str(got, exp)                                                                       \
	do {                                                                                       \
		const char *_g = (got);                                                            \
		const char *_e = (exp);                                                            \
		zassert_true(_g != NULL && strcmp(_g, _e) == 0, "got \"%s\", expected \"%s\"",     \
			     _g ? _g : "(null)", _e);                                              \
	} while (0)

#define expect_exit_within(max_ms, ret, reason)                                                    \
	do {                                                                                       \
		zassert_true(screen_wait(max_ms), "screen handler still running after %d ms",      \
			     (int)since_start());                                                  \
		zassert_equal(screen_ret, (ret), "handler returned %d, expected %d", screen_ret,   \
			      (ret));                                                              \
		if ((ret) == RET_TASK_EXITED) {                                                    \
			expect_str(screen_reason, (reason));                                       \
		}                                                                                  \
	} while (0)

#define expect_running_for(ms)                                                                     \
	zassert_false(screen_wait(ms), "screen handler returned %d (%s) after %d ms",              \
		      screen_ret, screen_reason ? screen_reason : "-", (int)done_after())

/* trigger_screen_state_change() calls from the log as "who->N", plus " (no change)" when ignored */
static const char *triggers(void)
{
	static char out[2048];
	static const char pfx[] = "trigger_screen_state_change(): ";
	static const char on_txt[] = ": trigger_on_screen -> ";
	static const char ign_txt[] = ": ignore:";
	size_t o = 0;

	out[0] = '\0';
	for (int i = 0; i < g1_fake_vlog_count(); i++) {
		const char *l = g1_fake_vlog_line(i);
		const char *on, *ign;

		if (strncmp(l, pfx, sizeof(pfx) - 1) != 0) {
			continue;
		}
		l += sizeof(pfx) - 1;
		on = strstr(l, on_txt);
		ign = strstr(l, ign_txt);
		if (on) {
			o += snprintf(out + o, sizeof(out) - o, "%s%.*s->%c", o ? "; " : "",
				      (int)(on - l), l, on[sizeof(on_txt) - 1]);
		} else if (ign) {
			o += snprintf(out + o, sizeof(out) - o, "%s%.*s->%c (no change)",
				      o ? "; " : "", (int)(ign - l), l, ign[sizeof(ign_txt) - 1]);
		}
		if (o >= sizeof(out)) {
			break;
		}
	}
	return out;
}

/* sync_to_slave() calls, e.g. "0c 0b 00 00; 08" */
static const char *syncs(void)
{
	static char out[1024];
	size_t o = 0;

	out[0] = '\0';
	for (int i = 0; i < g1_fake_sync_log.count && o < sizeof(out) - 32; i++) {
		char hex[64];

		g1_hex_format(g1_fake_sync_log.data[i], g1_fake_sync_log.len[i], hex, sizeof(hex));
		o += snprintf(out + o, sizeof(out) - o, "%s%s", o ? "; " : "", hex);
	}
	return out;
}

static void head_up(void)
{
	gs->imu.mode = 2;
}

static void head_down(void)
{
	gs->imu.mode = 1;
}

static void showing(int screen)
{
	gs->dashboard_mode = screen;
	gs->screen_on = 1;
}

static void persist_task(int task, int status)
{
	gs->task_status->task = task;
	gs->task_status->status = status;
	/* threads.c copies the record into the ESB sync package */
	gs->sync_status.task = task;
	gs->sync_status.status = status;
}

static void screen_before(void *fixture)
{
	ARG_UNUSED(fixture);
	gs = g1_state();
	gs->display_mode = 10;
	head_down();
}

static void screen_after(void *fixture)
{
	ARG_UNUSED(fixture);
	screen_stop();
}

static void screen_static_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	screen_stop();
	msg_busy = 0;
	msg_timeout_flag = 0;
	task_status_lock = 0;
}

G1_UNIT_RESET_HOOK(screen_static_reset);

ZTEST_SUITE(screen_state, NULL, NULL, screen_before, screen_after, NULL);

ZTEST(screen_state, test_reset_dash_buf_clear_empties_buffer_and_esb_counters)
{
	struct dash_data_buf *b = gs->dash_buf;

	gs->esb_up_seq = 1;
	gs->esb_down_seq = 2;
	gs->esb_up_id = 3;
	gs->esb_down_id = 4;
	gs->esb_up_recving = 1;
	gs->esb_down_recving = 1;
	b->type = 8;
	memset(b->data, 'x', sizeof(b->data));
	b->len = 0x1234;

	reset_dash_buf(1);

	zassert_equal(gs->esb_up_seq, 0);
	zassert_equal(gs->esb_down_seq, 0);
	zassert_equal(gs->esb_up_id, 0);
	zassert_equal(gs->esb_down_id, 0);
	zassert_equal(gs->esb_up_recving, 0);
	zassert_equal(gs->esb_down_recving, 0);
	zassert_equal(b->type, 0);
	for (int i = 0; i < (int)sizeof(b->data); i++) {
		zassert_equal(b->data[i], 0, "data[%d] = 0x%02x", i, b->data[i]);
	}
	zassert_equal(b->len, 0x1234);
}

ZTEST(screen_state, test_reset_dash_buf_keep_marks_first_byte_13_and_keeps_type)
{
	struct dash_data_buf *b = gs->dash_buf;

	b->type = 3;
	memset(b->data, 'x', sizeof(b->data));
	gs->esb_up_recving = 1;

	reset_dash_buf(0);

	zassert_equal(b->type, 3);
	zassert_equal(b->data[0], 13);
	zassert_equal(b->data[1], 0);
	zassert_equal(b->data[sizeof(b->data) - 1], 0);
	zassert_equal(gs->esb_up_recving, 0);
}

ZTEST(screen_state, test_msg_timeout_flag_needs_unread_messages_and_is_taken_once)
{
	set_msg_timeout_flag();
	zassert_equal(take_msg_timeout_flag(), 0, "flag set without unread messages");

	count_unread_messages_fake.return_val = 2;
	set_msg_timeout_flag();
	zassert_equal(take_msg_timeout_flag(), 1);
	zassert_equal(take_msg_timeout_flag(), 0, "flag not cleared by take");
}

/* 31536000 s = one year */
ZTEST(screen_state, test_msg_stay_time_per_display_mode)
{
	zassert_equal(get_msg_stay_time(10, 7), 7);
	zassert_equal(get_msg_stay_time(11, 7), 31536000);
	zassert_equal(get_msg_stay_time(12, 7), 1);
	zassert_equal(get_msg_stay_time(0, 9), 9);
}

ZTEST(screen_state, test_sync_message_signal_sends_cmd_7_with_signal)
{
	sync_to_slave_fake.return_val = 12;

	zassert_equal(sync_message_signal_to_slave(1), 12);
	expect_str(syncs(), "07 01");
}

ZTEST(screen_state, test_touch_with_unread_messages_marks_read_and_flags_touch)
{
	count_unread_messages_fake.return_val = 2;

	handle_msg_touch_key();

	zassert_equal(gs->unread_msg_num, 2);
	zassert_equal(clear_timeout_message_fake.call_count, 1);
	zassert_equal(clear_timeout_message_fake.arg0_val, 1);
	zassert_equal(mark_cur_message_read_fake.call_count, 1);
	zassert_equal(take_msg_timeout_flag(), 1);
}

ZTEST(screen_state, test_touch_on_shown_message_restarts_stay_time)
{
	count_unread_messages_fake.return_val = 1;
	g1_fake_msg_show_state = 4;
	gs->message_stay_time = 7;
	gs->stay_time_left_s = 1;

	handle_msg_touch_key();

	zassert_equal(gs->stay_time_left_s, 7);
}

ZTEST(screen_state, test_touch_without_unread_wakes_dashboard_when_head_up)
{
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

ZTEST(screen_state, test_touch_without_unread_head_down_keeps_screen_off)
{
	g1_fake_msg_show_state = 2;

	handle_msg_touch_key();

	zassert_equal(g1_fake_msg_show_state, 0);
	expect_str(triggers(), "");
	zassert_equal(gs->screen_on, 0);
}

/* 500 ms master exit delay + 150 ms dispatch sleep */
ZTEST(screen_state, test_dispatch_turns_screen_off_with_handler_reason)
{
	showing(2);
	persist_task(2, 2);

	screen_start(run_dispatch);

	expect_exit_within(1000, 0, NULL);
	expect_str(triggers(), "countdown:trigger-off-imm->0");
	zassert_equal(gs->screen_on, 0);
	zassert_equal(gs->dashboard_mode, 0);
	zassert_true(done_after() >= 500 + 150, "returned after %d ms", (int)done_after());
}

ZTEST(screen_state, test_dispatch_wakes_screen_for_running_task)
{
	gs->dashboard_mode = 9; /* not ready: exits at once */
	gs->screen_on = 0;

	screen_start(run_dispatch);

	expect_exit_within(2000, 0, NULL);
	expect_str(triggers(), "BLE:wakeup:running_task->1; temeprompter:self exit->0");
}

ZTEST(screen_state, test_dispatch_follows_screen_changes_until_a_task_exits)
{
	showing(2);
	persist_task(2, 2);
	gs->countdown_ts->enabled = 1;
	gs->countdown_ts->seconds = 60;
	head_up();

	screen_start(run_dispatch);
	expect_running_for(300);
	zassert_equal(gs->dashboard_mode, 6);

	head_down();
	expect_exit_within(500, 0, NULL);
	expect_str(triggers(), "dashboard:imu-trigger-off-imm->0");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dispatch_unknown_screen_sleeps_1s_and_returns)
{
	showing(3);

	screen_start(run_dispatch);

	expect_running_for(990);
	expect_exit_within(20, 0, NULL);
	expect_str(triggers(), "");
	zassert_equal(gs->dashboard_mode, 3);
}

ZTEST(screen_state, test_dispatch_expires_messages_and_releases_msg_busy)
{
	showing(3);
	msg_busy = 1;

	screen_start(run_dispatch);
	expect_exit_within(1100, 0, NULL);

	zassert_equal(clear_timeout_message_fake.call_count, 1);
	zassert_equal(clear_timeout_message_fake.arg0_val, 0);
	zassert_equal(msg_busy, 0);
}

ZTEST(screen_state, test_dispatch_keeps_msg_busy_for_message_screen)
{
	showing(4);
	get_msg_sync_state_fake.return_val = 4;
	msg_busy = 1;
	head_up();

	screen_start(run_dispatch);
	expect_running_for(100);

	zassert_equal(msg_busy, 1);
}

ZTEST(screen_state, test_dispatch_master_hides_expired_message_left_showing)
{
	showing(3);
	g1_fake_msg_show_state = 5;
	cur_message_show_state_fake.return_val = 2;

	screen_start(run_dispatch);
	expect_exit_within(1100, 0, NULL);

	expect_str(syncs(), "07 02");
	zassert_equal(g1_fake_msg_show_state, 0);
}

ZTEST(screen_state, test_dispatch_slave_leaves_expired_message_to_master)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	showing(3);
	g1_fake_msg_show_state = 5;
	cur_message_show_state_fake.return_val = 2;

	screen_start(run_dispatch);
	expect_exit_within(1100, 0, NULL);

	expect_str(syncs(), "");
	zassert_equal(g1_fake_msg_show_state, 5);
}

/* 6 = unread icon */
ZTEST(screen_state, test_dispatch_exit_with_new_message_shows_unread_icon)
{
	showing(2);
	persist_task(2, 2);
	g1_fake_msg_show_state = 1;
	gs->unread_msg_num = 2;

	screen_start(run_dispatch);
	expect_exit_within(1000, 0, NULL);

	zassert_equal(g1_fake_msg_show_state, 6);
}

static void idle_setup(void)
{
	gs->dashboard_mode = 0;
	gs->screen_on = 1;
	gs->suspend_state = 1;
	is_ble_device_bound_fake.return_val = true;
	get_glasses_ble_status_fake.return_val = 1;
}

ZTEST(screen_state, test_idle_entry_turns_screen_off_and_clears_task_record)
{
	idle_setup();
	persist_task(9, 1);
	atomic_set(&dashboard_flags, BIT(1) | BIT(0));
	gs->dash_buf->type = 5;
	gs->dash_buf->data[0] = 'A';
	msg_timeout_flag = 1;

	screen_start(process_for_IDLE);
	expect_running_for(200);

	expect_str(triggers(), "goto idle1->0");
	zassert_equal(gs->screen_on, 0);
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->task_status->task, 0);
	zassert_equal(gs->task_status->status, 1);
	zassert_equal(atomic_get(&dashboard_flags), BIT(0), "flags 0x%lx",
		      (long)atomic_get(&dashboard_flags));
	zassert_equal(gs->dash_buf->type, 0);
	zassert_equal(gs->dash_buf->data[0], 0);
	zassert_equal(msg_timeout_flag, 0);
}

ZTEST(screen_state, test_idle_entry_sets_message_icon_state_from_unread_count)
{
	idle_setup();
	g1_fake_msg_show_state = 2;
	gs->unread_msg_num = 3;

	screen_start(process_for_IDLE);
	expect_running_for(100);

	zassert_equal(g1_fake_msg_show_state, 6);
}

ZTEST(screen_state, test_idle_entry_keeps_showing_message_in_state_5)
{
	idle_setup();
	cur_message_show_state_fake.return_val = 1;

	screen_start(process_for_IDLE);
	expect_running_for(100);

	zassert_equal(g1_fake_msg_show_state, 5);
}

ZTEST(screen_state, test_idle_slave_entry_gives_sem_2)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	idle_setup();

	screen_start(process_for_IDLE);
	expect_running_for(100);

	zassert_equal(k_sem_count_get(&gs->sem_2), 1);
}

ZTEST(screen_state, test_idle_entry_with_head_up_keeps_screen_on)
{
	idle_setup();
	get_dashboard_startup_mode_fake.return_val = 2; /* head up does nothing */
	head_up();

	screen_start(process_for_IDLE);
	expect_running_for(200);

	expect_str(triggers(), "");
	zassert_equal(gs->screen_on, 1);
}

/* the master sets the 80 ms (0x50) IMU period on every idle poll; a 10 ms wait takes 11 ms here */
static int64_t first_idle_poll_ms;

static void record_first_poll(struct imu_ctx *c, uint32_t val)
{
	if (val == 0x50 && first_idle_poll_ms < 0) {
		first_idle_poll_ms = k_uptime_get() - screen_t0;
	}
	c->sample_period_ms = val;
}

ZTEST(screen_state, test_idle_waits_for_panel_suspend_before_polling)
{
	idle_setup();
	gs->suspend_state = 0;
	first_idle_poll_ms = -1;
	g1_fake_imu_set_fake.custom_fake = record_first_poll;

	screen_start(process_for_IDLE);
	g1_clock_advance_ms(300);
	zassert_equal(first_idle_poll_ms, -1, "polled at %d ms before the suspend",
		      (int)first_idle_poll_ms);
	gs->suspend_state = 1;
	expect_running_for(100);

	zassert_between_inclusive(first_idle_poll_ms, 300, 311, "first poll at %d ms",
				  (int)first_idle_poll_ms);
	zassert_equal(gs->imu.sample_period_ms, 0x50);
}

ZTEST(screen_state, test_idle_gives_up_waiting_for_panel_suspend_after_1s)
{
	idle_setup();
	gs->suspend_state = 0;
	first_idle_poll_ms = -1;
	g1_fake_imu_set_fake.custom_fake = record_first_poll;

	screen_start(process_for_IDLE);
	expect_running_for(1200);

	zassert_between_inclusive(first_idle_poll_ms, 1000, 100 * 11 + 11, "first poll at %d ms",
				  (int)first_idle_poll_ms);
}

/* canvas sync 0x0c: {0x0b, distance gear, height offset} */
ZTEST(screen_state, test_idle_head_up_opens_dashboard_with_canvas_sync_and_mic)
{
	idle_setup();
	gs->screen_on = 0;
	gs->panel.canvas_distance_gear = 5;
	gs->panel.raster_height_offset = 3;
	head_up();

	screen_start(process_for_IDLE);

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 6);
	zassert_equal(gs->dashboard_ts->dashboard_on, 1);
	expect_str(triggers(), "IMU:wakeup:dashboard->1");
	expect_str(syncs(), "0c 0b 05 03");
	zassert_equal(cal_panel_canvas_coord_fake.call_count, 1);
	zassert_equal(request_dmic_stream_start_fake.call_count, 1);
}

ZTEST(screen_state, test_idle_head_up_without_phone_link_starts_no_mic)
{
	idle_setup();
	get_glasses_ble_status_fake.return_val = 0;
	head_up();

	screen_start(process_for_IDLE);

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 6);
	zassert_equal(request_dmic_stream_start_fake.call_count, 0);
}

ZTEST(screen_state, test_idle_head_up_with_head_up_mic_disabled_starts_no_mic)
{
	idle_setup();
	gs->head_up_mic_disabled = 1;
	head_up();

	screen_start(process_for_IDLE);

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(request_dmic_stream_start_fake.call_count, 0);
}

ZTEST(screen_state, test_idle_head_up_with_mic_running_does_not_restart_it)
{
	idle_setup();
	dmic_get_stop_state_fake.custom_fake = NULL;
	dmic_get_stop_state_fake.return_val = 0;
	head_up();

	screen_start(process_for_IDLE);

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(request_dmic_stream_start_fake.call_count, 0);
}

ZTEST(screen_state, test_idle_slave_head_up_opens_dashboard_without_sync_or_mic)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	idle_setup();
	head_up();

	screen_start(process_for_IDLE);

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 6);
	expect_str(syncs(), "");
	zassert_equal(request_dmic_stream_start_fake.call_count, 0);
}

ZTEST(screen_state, test_idle_head_up_with_head_up_action_nothing_stays_idle)
{
	idle_setup();
	get_dashboard_startup_mode_fake.return_val = 2;
	head_up();

	screen_start(process_for_IDLE);
	expect_running_for(500);

	zassert_equal(gs->dashboard_mode, 0);
}

/* customize 4: double tap toggles the dashboard */
ZTEST(screen_state, test_idle_double_tap_dashboard_on_opens_dashboard)
{
	idle_setup();
	gs->double_tap_customize = 4;
	gs->double_tap_dashboard_on = 1;

	screen_start(process_for_IDLE);

	expect_exit_within(1100, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_dashboard_hold_opens_dashboard)
{
	idle_setup();
	gs->dashboard_ts->dashboard_hold = 1;

	screen_start(process_for_IDLE);

	expect_exit_within(1100, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 6);
}

static void expect_head_up_ignored(void)
{
	head_up();
	screen_start(process_for_IDLE);
	expect_running_for(500);
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->dashboard_ts->dashboard_on, 0);
}

ZTEST(screen_state, test_idle_head_up_ignored_in_case)
{
	idle_setup();
	is_in_box_fake.return_val = 1;
	expect_head_up_ignored();
}

ZTEST(screen_state, test_idle_head_up_ignored_in_silent_mode)
{
	idle_setup();
	gs->display_mode = 12;
	expect_head_up_ignored();
}

ZTEST(screen_state, test_idle_head_up_ignored_in_production_mode)
{
	idle_setup();
	get_pt_test_mode_fake.return_val = 1;
	expect_head_up_ignored();
}

ZTEST(screen_state, test_idle_head_up_ignored_when_not_bound)
{
	idle_setup();
	is_ble_device_bound_fake.return_val = false;
	expect_head_up_ignored();
}

ZTEST(screen_state, test_idle_head_up_ignored_during_onboarding)
{
	idle_setup();
	gs->onboarding->in_progress = 1;
	expect_head_up_ignored();
}

ZTEST(screen_state, test_idle_head_up_ignored_while_bitmap_syncs)
{
	idle_setup();
	gs->bitmap_info->sync_pending = 1;
	expect_head_up_ignored();
}

/* 0x26 = 38 ms IMU period */
ZTEST(screen_state, test_idle_resumes_running_persist_task)
{
	idle_setup();
	gs->screen_on = 0;
	persist_task(10, 2);
	gs->dashboard_ts->exit_pending = 1;
	gs->evenai_buf->state = 2;

	screen_start(process_for_IDLE);

	expect_exit_within(60, 0, NULL);
	zassert_equal(gs->dashboard_mode, 10);
	expect_str(triggers(), "BLE:wakeup:persist_task->1");
	zassert_equal(gs->dashboard_ts->exit_pending, 0);
	zassert_equal(gs->evenai_buf->state, 0);
	zassert_equal(g1_fake_imu_set_fake.arg1_val, 0x26);
}

ZTEST(screen_state, test_idle_resumes_paused_persist_task)
{
	idle_setup();
	persist_task(9, 3);

	screen_start(process_for_IDLE);

	expect_exit_within(60, 0, NULL);
	zassert_equal(gs->dashboard_mode, 9);
}

ZTEST(screen_state, test_idle_does_not_resume_persist_task_in_case)
{
	idle_setup();
	is_in_box_fake.return_val = 1;
	persist_task(10, 2);

	screen_start(process_for_IDLE);
	expect_running_for(500);

	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_idle_silent_mode_does_not_resume_other_persist_task)
{
	idle_setup();
	gs->display_mode = 12;
	persist_task(10, 2);

	screen_start(process_for_IDLE);
	expect_running_for(500);

	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_idle_dfu_returns_to_dispatcher)
{
	idle_setup();
	gs->work_mode = 1;

	screen_start(process_for_IDLE);

	expect_exit_within(60, 0, NULL);
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(g1_fake_imu_set_fake.arg1_val, 0x26);
}

ZTEST(screen_state, test_idle_case_dfu_returns_to_dispatcher)
{
	idle_setup();
	gs->work_mode = 8;

	screen_start(process_for_IDLE);

	expect_exit_within(60, 0, NULL);
}

ZTEST(screen_state, test_idle_waits_4s_for_pending_dashboard_exit)
{
	idle_setup();
	gs->dashboard_ts->exit_pending = 1;
	head_up();

	screen_start(process_for_IDLE);
	expect_running_for(3990);
	zassert_equal(gs->dashboard_ts->exit_pending, 1);
	zassert_equal(gs->dashboard_mode, 0);

	expect_exit_within(80 * POLL_MS - 3990 + 60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_ts->exit_pending, 0);
	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_waits_5s_for_even_ai_v2)
{
	idle_setup();
	gs->evenai_buf->state = 2;
	head_up();

	screen_start(process_for_IDLE);
	expect_running_for(4990);
	zassert_equal(gs->evenai_buf->state, 2);

	expect_exit_within(100 * POLL_MS - 4990 + 60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->evenai_buf->state, 0);
	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_idle_head_up_returns_to_message_still_showing)
{
	idle_setup();
	gs->screen_on = 0;
	is_message_showing_fake.return_val = true;
	count_unread_messages_fake.return_val = 1;
	head_up();

	screen_start(process_for_IDLE);

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 4);
	expect_str(triggers(), "IMU:wakeup:uncomplete msg->1");
	expect_str(syncs(), "07 01");
	zassert_equal(clear_timeout_message_fake.arg0_val, 0);
	zassert_equal(mark_cur_message_read_fake.call_count, 1);
	zassert_equal(set_particle_pending_fake.call_count, 1);
	zassert_equal(msg_timeout_flag, 1);
	zassert_equal(gs->dashboard_ts->dashboard_on, 0);
}

ZTEST(screen_state, test_idle_new_message_opens_new_message_screen)
{
	idle_setup();

	screen_start(process_for_IDLE);
	expect_running_for(100);
	g1_fake_msg_show_state = 1;

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 5);
	expect_str(triggers(), "goto idle1->0; BLE:wakeup:new-unread_msg->1");
	expect_str(syncs(), "07 00");
}

ZTEST(screen_state, test_idle_slave_new_message_waits_for_master)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	idle_setup();

	screen_start(process_for_IDLE);
	expect_running_for(100);
	g1_fake_msg_show_state = 1;
	expect_running_for(300);
	zassert_equal(gs->dashboard_mode, 0);

	gs->dashboard_mode = 5;
	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 5);
}

ZTEST(screen_state, test_idle_auto_display_shows_new_message_as_message_screen)
{
	idle_setup();
	gs->message_mode = 1;

	screen_start(process_for_IDLE);
	expect_running_for(100);
	g1_fake_msg_show_state = 1;

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 4);
	expect_str(triggers(), "goto idle1->0; IMU:wakeup:uncomplete msg->1");
	expect_str(syncs(), "07 01");
}

ZTEST(screen_state, test_idle_without_input_stays_idle)
{
	idle_setup();

	screen_start(process_for_IDLE);
	expect_running_for(3000);

	zassert_equal(gs->dashboard_mode, 0);
	zassert_true(g1_fake_imu_set_fake.call_count >= 55, "%d polls in 3 s",
		     g1_fake_imu_set_fake.call_count);
}

ZTEST(screen_state, test_wait_blow_head_turns_screen_off_and_waits_while_head_up)
{
	showing(1);
	head_up();

	screen_start(process_for_wait_blow_head);
	expect_running_for(1000);

	expect_str(triggers(), "WAIT_BLOW_HEAD->0");
	zassert_equal(gs->screen_on, 0);
	zassert_equal(gs->dashboard_mode, 1);
}

ZTEST(screen_state, test_wait_blow_head_head_down_goes_idle)
{
	showing(1);
	head_up();

	screen_start(process_for_wait_blow_head);
	expect_running_for(200);
	head_down();

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_wait_blow_head_double_tap_dashboard_setting_goes_idle)
{
	showing(1);
	head_up();
	gs->double_tap_customize = 4;

	screen_start(process_for_wait_blow_head);

	expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_wait_blow_head_even_ai_start_wakes_screen)
{
	showing(1);
	head_up();

	screen_start(process_for_wait_blow_head);
	expect_running_for(100);
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

	screen_start(process_for_countdown);
	expect_running_for(2000);

	zassert_equal(gs->dashboard_mode, 2);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_countdown_disabled_by_app_closes_to_idle)
{
	countdown_setup();

	screen_start(process_for_countdown);
	expect_running_for(200);
	gs->countdown_ts->enabled = 0;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "countdown:trigger-off-imm");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->task_status->status, 1);
	zassert_equal(display_close_screen_fake.arg0_val, 2);
	zassert_true(done_after() >= 200 + 500, "exit after %d ms", (int)done_after());
}

ZTEST(screen_state, test_countdown_slave_disabled_closes_without_delay)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	countdown_setup();
	gs->countdown_ts->enabled = 0;

	screen_start(process_for_countdown);

	expect_exit_within(10, RET_TASK_EXITED, "countdown:trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_countdown_disabled_with_head_up_waits_for_head_down)
{
	countdown_setup();
	gs->countdown_ts->enabled = 0;
	head_up();

	screen_start(process_for_countdown);

	expect_exit_within(600, RET_TASK_EXITED, "countdown:trigger-off-imm");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 1);
	zassert_equal(gs->task_status->task, 1);
	zassert_equal(gs->task_status->status, 1);
}

ZTEST(screen_state, test_countdown_closes_in_silent_mode)
{
	countdown_setup();

	screen_start(process_for_countdown);
	expect_running_for(100);
	gs->display_mode = 12;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "countdown:trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_countdown_dfu_closes_to_idle)
{
	countdown_setup();
	gs->work_mode = 1;

	screen_start(process_for_countdown);

	expect_exit_within(10, RET_TASK_EXITED, "countdown:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_countdown_time_over_stops_task)
{
	countdown_setup();

	screen_start(process_for_countdown);
	expect_running_for(100);
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

	screen_start(process_for_countdown);
	expect_running_for(100);
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

	screen_start(process_for_countdown);

	expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 4);
	zassert_equal(msg_timeout_flag, 1);
	zassert_equal(gs->dashboard_ts->dashboard_on, 0);
}

ZTEST(screen_state, test_countdown_wait_blow_head_request_ends_screen)
{
	countdown_setup();

	screen_start(process_for_countdown);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(60, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 1);
}

static void message_setup(void)
{
	showing(4);
	gs->unread_msg_num = 1;
	get_msg_sync_state_fake.return_val = 4;
	gs->message_stay_time = 5;
}

ZTEST(screen_state, test_message_show_entry_sets_show_state_and_stay_time)
{
	message_setup();
	gs->unknown_task_count = 7;
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(100);

	zassert_equal(g1_fake_msg_show_state, 4);
	zassert_equal(gs->stay_time_left_s, 5);
	zassert_equal(gs->unknown_task_count, 0);
	zassert_equal(get_msg_sync_state_fake.arg0_val, 0, "newest unread is index 0");
}

/* 1000 polls of 1 ms, 2 ms each here */
ZTEST(screen_state, test_message_show_waits_up_to_1s_for_slave_copy)
{
	message_setup();
	get_msg_sync_state_fake.return_val = 2;
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(990);
	zassert_equal(g1_fake_msg_show_state, 0);

	expect_running_for(1000 * 2 - 990 + 10);
	zassert_equal(g1_fake_msg_show_state, 4);
}

ZTEST(screen_state, test_message_show_stays_while_head_up_without_auto_display)
{
	message_setup();
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(10000);

	zassert_equal(gs->stay_time_left_s, 5);
	zassert_equal(gs->dashboard_mode, 4);
}

ZTEST(screen_state, test_message_show_head_down_closes_to_idle)
{
	message_setup();
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(300);
	head_down();

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "imu-trigger-off-imm");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(g1_fake_msg_show_state, 5);
	zassert_equal(display_close_screen_fake.arg0_val, 4);
}

/* the master counts 1 s per 20 polls while stay < 30 */
ZTEST(screen_state, test_message_show_auto_display_closes_after_stay_time)
{
	message_setup();
	gs->message_mode = 1;
	cur_message_show_state_fake.return_val = 1; /* on display, not expired */

	screen_start(process_for_message_show);
	expect_running_for(5 * 20 * 50 - 100);
	zassert_true(gs->stay_time_left_s <= 1, "stay %d", gs->stay_time_left_s);

	expect_exit_within(5 * 20 * POLL_MS - since_start() + POLL_MS + 501 + 10, RET_TASK_EXITED,
			   "imu-trigger-off-imm");
	zassert_equal(gs->stay_time_left_s, 0);
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_message_show_auto_display_long_stay_waits_for_expiry)
{
	message_setup();
	gs->message_mode = 1;
	gs->message_stay_time = 40;
	cur_message_show_state_fake.return_val = 1;

	screen_start(process_for_message_show);
	expect_running_for(5000);
	zassert_equal(gs->stay_time_left_s, 40);

	cur_message_show_state_fake.return_val = 2; /* expired */
	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "imu-trigger-off-imm");
}

ZTEST(screen_state, test_message_show_slave_does_not_count_stay_time)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	message_setup();
	gs->message_mode = 1;
	cur_message_show_state_fake.return_val = 1;

	screen_start(process_for_message_show);
	expect_running_for(7000);

	zassert_equal(gs->stay_time_left_s, 5);
}

ZTEST(screen_state, test_message_show_expired_with_head_up_goes_to_dashboard)
{
	message_setup();
	gs->message_mode = 1;
	gs->last_sync_ts = 5000;
	gs->dashboard_ts->exit_pending = 1;
	cur_message_show_state_fake.return_val = 2;
	head_up();

	screen_start(process_for_message_show);

	expect_exit_within(501 + 10, RET_SCREEN_CHANGED, NULL);
	zassert_true(done_after() >= 500);
	zassert_equal(gs->dashboard_mode, 6);
	zassert_equal(gs->dashboard_ts->dashboard_on, 1);
	zassert_equal(gs->last_sync_ts, 4000);
	zassert_equal(gs->dashboard_ts->exit_pending, 0);
	expect_str(syncs(), "0c 0b 00 00");
}

ZTEST(screen_state, test_message_show_expired_with_head_up_action_nothing_goes_idle)
{
	message_setup();
	gs->message_mode = 1;
	cur_message_show_state_fake.return_val = 2;
	get_dashboard_startup_mode_fake.return_val = 2;
	head_up();

	screen_start(process_for_message_show);

	expect_exit_within(501 + 10, RET_TASK_EXITED, "imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_message_show_expired_returns_to_double_tap_dashboard)
{
	message_setup();
	gs->message_mode = 1;
	cur_message_show_state_fake.return_val = 2;
	gs->double_tap_customize = 4;
	gs->double_tap_dashboard_on = 1;

	screen_start(process_for_message_show);

	expect_exit_within(501 + 10, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_message_show_slave_waits_for_master_to_close)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	message_setup();

	screen_start(process_for_message_show);
	expect_running_for(500);
	zassert_equal(g1_fake_msg_show_state, 4);

	gs->dashboard_mode = 0;
	expect_exit_within(60, RET_TASK_EXITED, "imu-trigger-off-imm");
	zassert_equal(g1_fake_msg_show_state, 5);
}

ZTEST(screen_state, test_message_show_closes_in_silent_mode)
{
	message_setup();
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(100);
	gs->display_mode = 12;

	expect_exit_within(60, RET_TASK_EXITED, "imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(g1_fake_msg_show_state, 5);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_message_show_closes_on_dfu)
{
	message_setup();
	head_up();
	gs->work_mode = 8;

	screen_start(process_for_message_show);

	expect_exit_within(10, RET_TASK_EXITED, "imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(g1_fake_msg_show_state, 5);
}

ZTEST(screen_state, test_message_show_follows_screen_change)
{
	message_setup();
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(100);
	gs->dashboard_mode = 6;

	expect_exit_within(60, RET_SCREEN_CHANGED, NULL);
}

ZTEST(screen_state, test_message_show_screen_set_idle_exits)
{
	message_setup();
	head_up();

	screen_start(process_for_message_show);
	expect_running_for(100);
	gs->dashboard_mode = 0;

	expect_exit_within(60, RET_TASK_EXITED, "imu-trigger-off-imm");
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

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(3000);

	zassert_equal(gs->dashboard_mode, 6);
	zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

ZTEST(screen_state, test_dashboard_entry_clears_flag_bit_1)
{
	dashboard_setup();
	atomic_set(&dashboard_flags, BIT(1) | BIT(0));

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(10);

	zassert_equal(atomic_get(&dashboard_flags), BIT(0));
}

ZTEST(screen_state, test_dashboard_head_down_closes_to_idle)
{
	dashboard_setup();

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(300);
	head_down();

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(dmic_stream_stop_fake.call_count, 1);
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(display_close_screen_fake.arg0_val, 6);
}

ZTEST(screen_state, test_dashboard_head_down_ignored_with_head_up_action_nothing)
{
	dashboard_setup();
	get_dashboard_startup_mode_fake.return_val = 2;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	head_down();
	expect_running_for(1000);

	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_double_tap_off_closes_with_head_up)
{
	dashboard_setup();
	gs->double_tap_customize = 4;
	gs->double_tap_dashboard_on = 1;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(300);
	gs->double_tap_dashboard_on = 0;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dashboard_hold_keeps_it_open_on_head_down)
{
	dashboard_setup();
	gs->dashboard_ts->dashboard_hold = 1;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	head_down();
	expect_running_for(1000);

	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_hold_release_closes_it_even_with_head_up)
{
	dashboard_setup();
	gs->dashboard_ts->dashboard_hold = 1;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(300);
	gs->dashboard_ts->dashboard_hold = 0;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dashboard_head_down_returns_to_persist_task)
{
	dashboard_setup();
	persist_task(2, 3); /* countdown: not a task the dashboard switches to */

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(200);
	head_down();

	expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 2);
	zassert_true(dmic_stream_stop_fake.call_count >= 1);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_dashboard_switches_to_started_navigation)
{
	dashboard_setup();

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(200);
	persist_task(10, 2);

	expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 10);
	zassert_equal(dmic_stream_stop_fake.call_count, 1, "navigation does not use the mic");
}

ZTEST(screen_state, test_dashboard_switch_to_mic_task_keeps_mic)
{
	dashboard_setup();
	persist_task(9, 2);

	screen_start(process_for_DASHBOARD_show);

	expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 9);
	zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

ZTEST(screen_state, test_dashboard_switches_to_each_known_task_id)
{
	static const uint8_t ids[] = {7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		dashboard_setup();
		persist_task(ids[i], 2);
		screen_start(process_for_DASHBOARD_show);
		expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
		zassert_equal(gs->dashboard_mode, ids[i], "task %d", ids[i]);
		screen_stop();
	}
}

/* polls at 0, 51, ... 459 ms */
ZTEST(screen_state, test_dashboard_counts_unknown_synced_task)
{
	dashboard_setup();
	persist_task(2, 2);
	gs->unknown_task_count = 9;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(490);

	zassert_equal(gs->dashboard_mode, 6);
	zassert_equal(gs->unknown_task_count, 10, "%d", gs->unknown_task_count);
}

/* cmd 6 {2, 9} closes the slave's dashboard */
ZTEST(screen_state, test_dashboard_exit_request_closes_page_then_goes_to_task)
{
	dashboard_setup();
	persist_task(16, 2);
	gs->dashboard_ts->exit_pending = 1;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(300);
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

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(300);
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

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(500);

	zassert_equal(gs->dashboard_ts->exit_pending, 1);
	expect_str(syncs(), "");
	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_head_down_with_exit_pending_goes_idle)
{
	dashboard_setup();
	g1_unit_set_role(G1_ROLE_SLAVE); /* exit_pending stays 1 */
	persist_task(16, 2);
	gs->dashboard_ts->exit_pending = 1;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	head_down();

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(dmic_stream_stop_fake.call_count, 1);
}

ZTEST(screen_state, test_dashboard_follows_message_screen_request)
{
	dashboard_setup();

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	gs->dashboard_mode = 4;

	expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
	zassert_equal(dmic_stream_stop_fake.call_count, 1);
}

ZTEST(screen_state, test_dashboard_touch_with_messages_shows_message)
{
	dashboard_setup();
	count_messages_fake.return_val = 2;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	msg_timeout_flag = 1;

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
	msg_timeout_flag = 1;

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(500);

	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_returns_to_dispatcher_when_suspended)
{
	dashboard_setup();
	gs->suspend_state = 1;

	screen_start(process_for_DASHBOARD_show);

	expect_exit_within(POLL_MS + 1, 0, NULL);
	zassert_equal(gs->dashboard_mode, 6);
}

ZTEST(screen_state, test_dashboard_closes_on_dfu)
{
	dashboard_setup();

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	gs->work_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_dashboard_wait_blow_head_request_ends_screen)
{
	dashboard_setup();

	screen_start(process_for_DASHBOARD_show);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 1);
}

static void unread_setup(void)
{
	showing(8);
	gs->schedule_notif_pending = 1;
	count_unread_messages_fake.return_val = 3;
	head_up();
}

ZTEST(screen_state, test_unread_msg_entry_takes_schedule_flag_and_counts_unread)
{
	unread_setup();

	screen_start(process_for_unread_msg);
	expect_running_for(500);

	zassert_equal(gs->schedule_notif_pending, 0);
	zassert_equal(gs->unread_msg_num, 3);
}

ZTEST(screen_state, test_unread_msg_head_down_closes_to_idle)
{
	unread_setup();

	screen_start(process_for_unread_msg);
	expect_running_for(300);
	head_down();

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "imu-trigger-off-imm");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(display_close_screen_fake.arg0_val, 8);
}

/* DEFECT: process_for_unread_msg has no DFU exit; its only exit needs display mode 10. */
ZTEST_EXPECT_FAIL(screen_state, test_unread_msg_closes_on_dfu);
ZTEST(screen_state, test_unread_msg_closes_on_dfu)
{
	unread_setup();

	screen_start(process_for_unread_msg);
	expect_running_for(100);
	gs->work_mode = 1;

	expect_exit_within(1000, RET_TASK_EXITED, "imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

static void teleprompter_setup(void)
{
	showing(9);
	persist_task(9, 2);
	gs->teleprompter->ready = 1;
}

ZTEST(screen_state, test_teleprompter_stays_while_ready)
{
	teleprompter_setup();

	screen_start(process_for_teleprompter);
	expect_running_for(2000);

	zassert_equal(gs->dashboard_mode, 9);
	zassert_equal(dmic_stream_stop_fake.call_count, 0);
}

/* the reason string is misspelled in the source */
ZTEST(screen_state, test_teleprompter_stopped_by_app_closes_to_idle)
{
	teleprompter_setup();

	screen_start(process_for_teleprompter);
	expect_running_for(200);
	gs->teleprompter->ready = 0;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "temeprompter:self exit");
	zassert_true(done_after() >= 200 + 500);
	zassert_equal(dmic_stream_stop_fake.call_count, 1);
	expect_str(syncs(), "08");
	zassert_equal(teleprompter_state_reset_fake.call_count, 1);
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->task_status->status, 1);
}

ZTEST(screen_state, test_teleprompter_stopped_with_head_up_wakes_for_dashboard)
{
	teleprompter_setup();
	gs->screen_on = 0;
	gs->teleprompter->ready = 0;
	head_up();

	screen_start(process_for_teleprompter);

	expect_exit_within(501 + 301 + 5, RET_TASK_EXITED, "temeprompter:self exit");
	zassert_true(done_after() >= 800, "exit after %d ms", (int)done_after());
	expect_str(triggers(), "IMU:wakeup:dashboard->1");
	expect_str(syncs(), "08");
	zassert_equal(teleprompter_state_reset_fake.call_count, 1);
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_teleprompter_closes_on_dfu)
{
	teleprompter_setup();
	gs->work_mode = 8;

	screen_start(process_for_teleprompter);

	expect_exit_within(10, RET_TASK_EXITED, "teleprompter:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_teleprompter_wait_blow_head_request_ends_screen)
{
	teleprompter_setup();

	screen_start(process_for_teleprompter);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
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

	screen_start(process_for_navigation);
	expect_running_for(2000);

	zassert_equal(gs->dashboard_mode, 10);
}

ZTEST(screen_state, test_navigation_exit_closes_to_idle)
{
	navigation_setup();

	screen_start(process_for_navigation);
	expect_running_for(200);
	gs->nav->running = 0;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "navigation:self exit");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	expect_str(triggers(), "");
}

ZTEST(screen_state, test_navigation_exit_with_head_up_wakes_for_dashboard)
{
	navigation_setup();
	gs->screen_on = 0;
	gs->nav->running = 0;
	head_up();

	screen_start(process_for_navigation);

	expect_exit_within(501 + 301 + 5, RET_TASK_EXITED, "navigation:self exit");
	zassert_true(done_after() >= 800);
	expect_str(triggers(), "IMU:wakeup:dashboard->1");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_navigation_closes_on_dfu)
{
	navigation_setup();
	gs->work_mode = 1;

	screen_start(process_for_navigation);

	expect_exit_within(10, RET_TASK_EXITED, "navigation:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_navigation_wait_blow_head_request_ends_screen)
{
	navigation_setup();

	screen_start(process_for_navigation);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
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

	screen_start(process_for_translate);
	expect_running_for(2000);

	zassert_equal(gs->dashboard_mode, 11);
}

ZTEST(screen_state, test_translate_both_texts_cleared_closes_to_idle)
{
	translate_setup();

	screen_start(process_for_translate);
	expect_running_for(200);
	gs->translate_src->data[0] = 0;
	gs->translate_dst->data[0] = 0;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "translate:self exit");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
}

/* no screen wake, unlike teleprompter / navigation */
ZTEST(screen_state, test_translate_cleared_with_head_up_closes_after_800ms)
{
	translate_setup();
	gs->translate_src->data[0] = 0;
	gs->translate_dst->data[0] = 0;
	head_up();

	screen_start(process_for_translate);

	expect_exit_within(501 + 301 + 5, RET_TASK_EXITED, "translate:self exit");
	zassert_true(done_after() >= 800);
	expect_str(triggers(), "");
}

ZTEST(screen_state, test_translate_closes_on_dfu)
{
	translate_setup();
	gs->work_mode = 8;

	screen_start(process_for_translate);

	expect_exit_within(10, RET_TASK_EXITED, "translate:self exit");
	zassert_equal(gs->dashboard_mode, 0);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_translate_wait_blow_head_request_ends_screen)
{
	translate_setup();

	screen_start(process_for_translate);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
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

	screen_start(process_for_quicknote);
	expect_running_for(2000);

	zassert_equal(gs->dashboard_mode, 12);
}

ZTEST(screen_state, test_quicknote_disabled_head_down_closes_to_idle)
{
	quicknote_setup();

	screen_start(process_for_quicknote);
	expect_running_for(200);
	gs->quicknote_show_info->enable = 0;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "quicknote:self exit");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_quicknote_disabled_head_up_waits_for_head_down)
{
	quicknote_setup();
	gs->screen_on = 0;
	gs->quicknote_show_info->enable = 0;
	head_up();

	screen_start(process_for_quicknote);

	expect_exit_within(501 + 5, RET_TASK_EXITED, "quicknote:self exit");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 1);
	zassert_equal(gs->task_status->task, 1);
	expect_str(triggers(), "IMU:wakeup:dashboard->1");
}

ZTEST(screen_state, test_quicknote_closes_on_dfu)
{
	quicknote_setup();
	gs->work_mode = 1;

	screen_start(process_for_quicknote);

	expect_exit_within(10, RET_TASK_EXITED, "quicknote:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_quicknote_wait_blow_head_request_ends_screen)
{
	quicknote_setup();

	screen_start(process_for_quicknote);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "dashboard:imu-trigger-off-imm");
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

	screen_start(run_not_disturb);
	expect_running_for(100);

	zassert_equal(gs->bitmap_info->sync_pending, 0);
}

/* no sync 8: the bitmap is lens-local */
ZTEST(screen_state, test_not_disturb_closes_1500ms_after_deadline)
{
	bitmap_setup();

	screen_start(run_not_disturb);
	expect_running_for(1500);
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

	screen_start(run_not_disturb);
	expect_running_for(300);
	gs->bitmap_info->enable = 0;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "not_disturb:self exit");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_not_disturb_closes_on_dfu)
{
	bitmap_setup();
	gs->work_mode = 1;

	screen_start(run_not_disturb);

	expect_exit_within(10, RET_TASK_EXITED, "not_disturb:dfu");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_not_disturb_wait_blow_head_request_ends_screen)
{
	bitmap_setup();
	gs->bitmap_info->deadline = k_uptime_get() + 100000;

	screen_start(run_not_disturb);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "not_disturb:imu-trigger-off-imm");
}

struct simple_task {
	int screen;
	screen_fn fn;
	const char *name;
	int master_delay_ms;
	uint8_t *flag;
};

static struct simple_task simple(int screen)
{
	switch (screen) {
	case 14:
		return (struct simple_task){14, process_for_onboarding, "onboarding", 100,
					    &gs->onboarding->active};
	case 15:
		return (struct simple_task){15, process_for_raster_height, "raster_height", 500,
					    &gs->raster_height_cfg[0]};
	case 17:
		return (struct simple_task){17, process_for_set_imu_pitch, "set_imu_pitch", 500,
					    &gs->horiz_pitch_cfg[0]};
	default:
		return (struct simple_task){19, process_for_transcribe, "transcribe", 500,
					    (uint8_t *)&gs->transcribe_buf->running};
	}
}

static void simple_task_flag_cleared_closes(int screen)
{
	struct simple_task t = simple(screen);
	char reason[48];

	showing(screen);
	persist_task(screen, 2);
	*t.flag = 1;
	screen_start(t.fn);
	expect_running_for(500);
	zassert_equal(gs->dashboard_mode, screen);

	*t.flag = 0;
	snprintf(reason, sizeof(reason), "%s:self exit", t.name);
	expect_exit_within(POLL_MS + t.master_delay_ms + 1 + 5, RET_TASK_EXITED, reason);
	zassert_true(done_after() >= 500 + t.master_delay_ms, "exit after %d ms",
		     (int)done_after());
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->task_status->status, 1);
}

static void simple_task_closes_on_dfu(int screen)
{
	struct simple_task t = simple(screen);
	char reason[48];

	showing(screen);
	*t.flag = 1;
	gs->work_mode = 1;
	screen_start(t.fn);
	snprintf(reason, sizeof(reason), "%s:dfu", t.name);
	expect_exit_within(10, RET_TASK_EXITED, reason);
	zassert_equal(gs->dashboard_mode, 0);
	expect_str(syncs(), "");
}

static void simple_task_wait_blow_head_request_ends(int screen)
{
	struct simple_task t = simple(screen);
	char reason[48];

	showing(screen);
	*t.flag = 1;
	screen_start(t.fn);
	expect_running_for(100);
	gs->dashboard_mode = 1;
	snprintf(reason, sizeof(reason), "%s:imu-trigger-off-imm", t.name);
	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, reason);
}

ZTEST(screen_state, test_onboarding_inactive_closes_to_idle_after_100ms)
{
	simple_task_flag_cleared_closes(14);
}

ZTEST(screen_state, test_onboarding_closes_on_dfu)
{
	simple_task_closes_on_dfu(14);
}

ZTEST(screen_state, test_onboarding_wait_blow_head_request_ends_screen)
{
	simple_task_wait_blow_head_request_ends(14);
}

ZTEST(screen_state, test_raster_height_done_closes_to_idle_after_500ms)
{
	simple_task_flag_cleared_closes(15);
}

ZTEST(screen_state, test_raster_height_closes_on_dfu)
{
	simple_task_closes_on_dfu(15);
}

ZTEST(screen_state, test_raster_height_wait_blow_head_request_ends_screen)
{
	simple_task_wait_blow_head_request_ends(15);
}

ZTEST(screen_state, test_set_imu_pitch_done_closes_to_idle_after_500ms)
{
	simple_task_flag_cleared_closes(17);
}

ZTEST(screen_state, test_set_imu_pitch_closes_on_dfu)
{
	simple_task_closes_on_dfu(17);
}

ZTEST(screen_state, test_set_imu_pitch_wait_blow_head_request_ends_screen)
{
	simple_task_wait_blow_head_request_ends(17);
}

ZTEST(screen_state, test_transcribe_stopped_closes_to_idle_after_500ms)
{
	simple_task_flag_cleared_closes(19);
}

ZTEST(screen_state, test_transcribe_closes_on_dfu)
{
	simple_task_closes_on_dfu(19);
}

ZTEST(screen_state, test_transcribe_wait_blow_head_request_ends_screen)
{
	simple_task_wait_blow_head_request_ends(19);
}

ZTEST(screen_state, test_transcribe_slave_stopped_closes_without_delay)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	showing(19);
	gs->transcribe_buf->running = 0;

	screen_start(process_for_transcribe);

	expect_exit_within(10, RET_TASK_EXITED, "transcribe:self exit");
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

	screen_start(process_for_prompt);
	expect_running_for(4000);
	zassert_equal(gs->ts_info->enable, 1);

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "prompt:self exit");
	zassert_equal(gs->ts_info->enable, 0);
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_prompt_slave_does_not_time_out)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	prompt_setup();

	screen_start(process_for_prompt);
	expect_running_for(6000);

	zassert_equal(gs->ts_info->enable, 1);
}

ZTEST(screen_state, test_prompt_disabled_closes_to_idle)
{
	prompt_setup();

	screen_start(process_for_prompt);
	expect_running_for(300);
	gs->ts_info->enable = 0;

	expect_exit_within(POLL_MS + 501 + 5, RET_TASK_EXITED, "prompt:self exit");
}

ZTEST(screen_state, test_prompt_closes_on_dfu)
{
	prompt_setup();
	gs->work_mode = 8;

	screen_start(process_for_prompt);

	expect_exit_within(10, RET_TASK_EXITED, "prompt:dfu");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_prompt_wait_blow_head_request_ends_screen)
{
	prompt_setup();

	screen_start(process_for_prompt);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "prompt:imu-trigger-off-imm");
}

static void evenai_setup(void)
{
	showing(16);
	persist_task(16, 2);
	gs->dash_buf->type = 3;
	gs->dash_buf->data[0] = 'A';
	get_glasses_ble_status_fake.return_val = 1;
}

ZTEST(screen_state, test_evenai_stays_while_answer_shown)
{
	evenai_setup();

	screen_start(process_for_evenai);
	expect_running_for(2000);

	zassert_equal(gs->dashboard_mode, 16);
	zassert_equal(gs->dash_buf->type, 3);
}

ZTEST(screen_state, test_evenai_empty_buffer_closes_and_reports_to_phone)
{
	evenai_setup();

	screen_start(process_for_evenai);
	expect_running_for(200);
	gs->dash_buf->data[0] = 0;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai:self exit");
	zassert_equal(report_event_to_phone_fake.call_count, 1);
	zassert_equal(report_event_to_phone_fake.arg0_val, 0);
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->dash_buf->type, 0);
}

ZTEST(screen_state, test_evenai_slave_closes_without_phone_event)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	evenai_setup();
	gs->dash_buf->type = 0;

	screen_start(process_for_evenai);

	expect_exit_within(10, RET_TASK_EXITED, "evenai:self exit");
	zassert_equal(report_event_to_phone_fake.call_count, 0);
}

/* type 10: disconnected page */
ZTEST(screen_state, test_evenai_phone_link_lost_switches_to_type_10)
{
	evenai_setup();

	screen_start(process_for_evenai);
	expect_running_for(200);
	get_glasses_ble_status_fake.return_val = 0;
	expect_running_for(POLL_MS + 1);

	zassert_equal(gs->dash_buf->type, 10);
	zassert_equal(gs->dashboard_mode, 16);
}

ZTEST(screen_state, test_evenai_timed_answer_closes_after_stay_time)
{
	evenai_setup();
	gs->dash_buf->type = 8;
	gs->stay_time_left_s = 3;

	screen_start(process_for_evenai);
	expect_running_for(3 * 20 * 50 - 100);

	expect_exit_within(3 * 20 * POLL_MS - since_start() + POLL_MS + 5, RET_TASK_EXITED,
			   "evenai:self exit");
	zassert_equal(gs->stay_time_left_s, 0);
	zassert_equal(report_event_to_phone_fake.arg0_val, 0);
}

/* the counter wraps to 255 after 16 s; nothing reads it for type 7 */
ZTEST(screen_state, test_evenai_type_7_page_is_not_closed_by_stay_time)
{
	evenai_setup();
	gs->dash_buf->type = 7;
	gs->stay_time_left_s = 16;

	screen_start(process_for_evenai);
	expect_running_for(20000);

	zassert_equal(gs->dashboard_mode, 16);
	zassert_equal(gs->dash_buf->type, 7);
}

ZTEST(screen_state, test_evenai_closes_in_silent_mode)
{
	evenai_setup();

	screen_start(process_for_evenai);
	expect_running_for(100);
	gs->display_mode = 12;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai:self exit");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->dash_buf->data[0], 0);
}

ZTEST(screen_state, test_evenai_closes_on_dfu)
{
	evenai_setup();
	gs->work_mode = 1;

	screen_start(process_for_evenai);

	expect_exit_within(10, RET_TASK_EXITED, "evenai:self exit");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(gs->dash_buf->type, 0);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_evenai_wait_blow_head_request_clears_buffer)
{
	evenai_setup();

	screen_start(process_for_evenai);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai:imu-trigger-off-imm");
	zassert_equal(gs->dash_buf->type, 0);
	zassert_equal(gs->dash_buf->data[0], 0);
}

static void evenai_v2_setup(void)
{
	showing(20);
	persist_task(20, 2);
	gs->evenai_buf->state = 1;
	gs->evenai_buf->sub = 3;
}

ZTEST(screen_state, test_even_ai_v2_entry_stamps_time_and_drops_exit_request)
{
	evenai_v2_setup();
	gs->dashboard_ts->exit_pending = 1;
	g1_clock_advance_ms(1234);

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(100);

	zassert_equal(gs->evenai_buf->ts, screen_t0);
	zassert_equal(gs->dashboard_ts->exit_pending, 0);
}

ZTEST(screen_state, test_even_ai_v2_slave_entry_gives_sem_2)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	evenai_v2_setup();

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(100);

	zassert_equal(k_sem_count_get(&gs->sem_2), 1);
}

ZTEST(screen_state, test_even_ai_v2_finished_closes_to_idle)
{
	evenai_v2_setup();

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(300);
	gs->evenai_buf->state = 0;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_even_ai_v2_master_state_2_closes)
{
	evenai_v2_setup();
	gs->evenai_buf->state = 2;

	screen_start(process_for_even_ai_v2_show);

	expect_exit_within(10, RET_TASK_EXITED, "evenai_v2:self exit");
	zassert_equal(gs->evenai_buf->state, 3);
}

ZTEST(screen_state, test_even_ai_v2_slave_state_2_stays)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	evenai_v2_setup();
	gs->evenai_buf->state = 2;

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(500);

	zassert_equal(gs->evenai_buf->state, 2);
}

ZTEST(screen_state, test_even_ai_v2_listening_times_out_after_15s)
{
	evenai_v2_setup();
	gs->evenai_buf->sub = 2;

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(15000);
	zassert_equal(gs->evenai_buf->state, 1);

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
	zassert_equal(gs->evenai_buf->state, 0);
}

ZTEST(screen_state, test_even_ai_v2_sub7_action1_times_out_after_5s)
{
	evenai_v2_setup();
	gs->evenai_buf->sub = 7;
	gs->evenai_buf->action_status = 1;

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(5000);

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
}

ZTEST(screen_state, test_even_ai_v2_sub4_action13_times_out_after_5s)
{
	evenai_v2_setup();
	gs->evenai_buf->sub = 4;
	gs->evenai_buf->action_status = 0x13;

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(5000);

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
}

ZTEST(screen_state, test_even_ai_v2_answer_does_not_time_out)
{
	evenai_v2_setup();

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(20000);

	zassert_equal(gs->evenai_buf->state, 1);
}

ZTEST(screen_state, test_even_ai_v2_slave_listening_does_not_time_out)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	evenai_v2_setup();
	gs->evenai_buf->sub = 2;

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(20000);

	zassert_equal(gs->evenai_buf->state, 1);
}

ZTEST(screen_state, test_even_ai_v2_close_retried_until_slave_acks)
{
	evenai_v2_setup();
	gs->evenai_buf->state = 0;
	sync_to_slave_fake.return_val = 3000;

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(300);
	zassert_true(g1_fake_sync_log.count >= 5, "%d attempts", g1_fake_sync_log.count);
	zassert_equal(gs->dashboard_mode, 20);

	sync_to_slave_fake.return_val = 0;
	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:self exit");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_even_ai_v2_closes_on_dfu)
{
	evenai_v2_setup();
	gs->work_mode = 1;

	screen_start(process_for_even_ai_v2_show);

	expect_exit_within(10, RET_TASK_EXITED, "evenai_v2:dfu");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_even_ai_v2_wait_blow_head_request_ends_screen)
{
	evenai_v2_setup();

	screen_start(process_for_even_ai_v2_show);
	expect_running_for(100);
	gs->dashboard_mode = 1;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "evenai_v2:imu-trigger-off-imm");
}

static void new_message_setup(void)
{
	showing(5);
	gs->unread_msg_num = 1;
	count_unread_messages_fake.return_val = 1;
	g1_fake_msgs[0].recv_time = 1000;
	gs->message_stay_time = 3;
}

ZTEST(screen_state, test_new_message_entry_sets_show_state_and_stay_time)
{
	new_message_setup();

	screen_start(process_for_new_message_come_on);
	expect_running_for(100);

	zassert_equal(g1_fake_msg_show_state, 2);
	zassert_equal(gs->stay_time_left_s, 3);
}

ZTEST(screen_state, test_new_message_times_out_after_stay_time)
{
	new_message_setup();

	screen_start(process_for_new_message_come_on);
	expect_running_for(3 * 20 * 50 - 100);
	zassert_equal(gs->stay_time_left_s, 1);

	expect_exit_within(3 * 20 * POLL_MS - since_start() + 5, RET_TASK_EXITED,
			   "new-msg-timeout-goto-idle");
	expect_str(syncs(), "08");
	zassert_equal(gs->dashboard_mode, 0);
	zassert_equal(g1_fake_msg_show_state, 6);
}

ZTEST(screen_state, test_new_message_head_up_shows_message)
{
	new_message_setup();

	screen_start(process_for_new_message_come_on);
	expect_running_for(500);
	head_up();

	expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 4);
	expect_str(syncs(), "07 01");
	zassert_equal(clear_timeout_message_fake.arg0_val, 0);
	zassert_equal(msg_timeout_flag, 1);
	zassert_equal(mark_cur_message_read_fake.call_count, 1);
	zassert_equal(set_particle_pending_fake.call_count, 1);
	expect_str(triggers(), "IMU:wakeup:new-notification->1 (no change)");
}

ZTEST(screen_state, test_new_message_display_mode_11_shows_message)
{
	new_message_setup();
	gs->display_mode = 11;

	screen_start(process_for_new_message_come_on);

	expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 4);
}

/*
 * DEFECT: update_temp_task_status() refuses screen 8 over screen 5, yet SCREEN_CHANGED is returned,
 * so the dispatcher re-enters screen 5 in a busy loop. Run directly: that spin would stop simulated
 * time.
 */
ZTEST_EXPECT_FAIL(screen_state, test_new_message_head_up_with_schedule_opens_unread_screen);
ZTEST(screen_state, test_new_message_head_up_with_schedule_opens_unread_screen)
{
	new_message_setup();
	gs->schedule_notif_pending = 1;
	head_up();

	screen_start(process_for_new_message_come_on);

	expect_exit_within(10, RET_SCREEN_CHANGED, NULL);
	zassert_equal(gs->dashboard_mode, 8);
	zassert_equal(g1_fake_msg_show_state, 4);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_new_message_slave_waits_for_master)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	new_message_setup();
	head_up();

	screen_start(process_for_new_message_come_on);
	expect_running_for(5000);
	zassert_equal(gs->stay_time_left_s, 3);
	zassert_equal(gs->dashboard_mode, 5);

	gs->dashboard_mode = 4;
	expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
}

/* decrements at 3, 4 and 5 s on the same 20-poll grid */
ZTEST(screen_state, test_new_message_newer_message_restarts_stay_time)
{
	new_message_setup();

	screen_start(process_for_new_message_come_on);
	expect_running_for(2500);
	zassert_equal(gs->stay_time_left_s, 1);
	g1_fake_msgs[1].recv_time = 1002;
	gs->unread_msg_num = 2;
	expect_running_for(POLL_MS + 1);
	zassert_equal(gs->stay_time_left_s, 3);

	expect_running_for(4900 - since_start());
	expect_exit_within(5 * 20 * POLL_MS - since_start() + 5, RET_TASK_EXITED,
			   "new-msg-timeout-goto-idle");
}

/* DEFECT: with no unread message, entry unread_msg_num - 1 = -1 is read. */
ZTEST_EXPECT_FAIL(screen_state, test_new_message_without_unread_reads_inside_queue);
ZTEST(screen_state, test_new_message_without_unread_reads_inside_queue)
{
	new_message_setup();
	gs->unread_msg_num = 0;

	screen_start(process_for_new_message_come_on);
	expect_running_for(200);

	zassert_equal(g1_fake_msg_bad_index, 0, "%d reads of entry -1", g1_fake_msg_bad_index);
}

ZTEST(screen_state, test_new_message_closes_in_silent_mode)
{
	new_message_setup();

	screen_start(process_for_new_message_come_on);
	expect_running_for(100);
	gs->display_mode = 12;

	expect_exit_within(POLL_MS + 1, RET_TASK_EXITED, "message:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
	expect_str(syncs(), "");
}

ZTEST(screen_state, test_new_message_closes_on_dfu)
{
	new_message_setup();
	gs->work_mode = 1;

	screen_start(process_for_new_message_come_on);

	expect_exit_within(10, RET_TASK_EXITED, "message:imu-trigger-off-imm");
	zassert_equal(gs->dashboard_mode, 0);
}

ZTEST(screen_state, test_new_message_closes_on_case_dfu)
{
	new_message_setup();
	gs->work_mode = 8;

	screen_start(process_for_new_message_come_on);

	expect_exit_within(10, RET_TASK_EXITED, "message:imu-trigger-off-imm");
}

ZTEST(screen_state, test_new_message_follows_screen_change)
{
	new_message_setup();

	screen_start(process_for_new_message_come_on);
	expect_running_for(100);
	gs->dashboard_mode = 6;

	expect_exit_within(POLL_MS + 1, RET_SCREEN_CHANGED, NULL);
}
