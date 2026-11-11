/*
 * Screen state machine: message_task.c's screen handlers (screen_test.c includes the file),
 * task_status.c and system_state.c.
 * A handler runs in a worker thread (screen_start()) while the test thread advances simulated time
 * and changes inputs between its 50 ms polls.
 * Screens (dashboard_mode): 0 idle, 1 wait for head-down, 2 countdown, 4 message, 5 new message,
 * 6 dashboard, 7 bitmap, 8 unread message, 9 teleprompter, 10 navigation, 11 translate,
 * 12 quicknote, 14 onboarding, 15 raster height, 16 even AI, 17 IMU pitch, 18 prompt,
 * 19 transcribe, 20 even AI v2. Task status: 1 stopped, 2 running, 3 paused.
 * suspend_state: 0 running, 1 suspended, 2 suspending, 3 resuming.
 * imu.mode 2 head up, 1 head down; display_mode 10 normal, 12 silent; work_mode 1 / 8 DFU.
 * Sync 8 closes the slave's screen; the master waits 500 ms before a self exit.
 * Every test starts on the master in display mode 10 with the head down.
 */
#ifndef G1_UNIT_SCREEN_TEST_H_
#define G1_UNIT_SCREEN_TEST_H_

#include "fakes.h"
#include "threads/threads.h"

/* sync_to_slave() logs cmd and data, and sets sync_ack to 2 if return_val < 3000 ms, else 1 */
extern struct g1_pkt_log g1_fake_sync_log;
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_glasses_ble_status);
DECLARE_FAKE_VALUE_FUNC(uint8_t, count_unread_messages);
DECLARE_FAKE_VALUE_FUNC(uint8_t, count_messages);
DECLARE_FAKE_VOID_FUNC(clear_timeout_message, int);
DECLARE_FAKE_VOID_FUNC(mark_cur_message_read);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_msg_sync_state, int);
DECLARE_FAKE_VALUE_FUNC(bool, is_message_showing);
DECLARE_FAKE_VOID_FUNC(set_particle_pending);
DECLARE_FAKE_VOID_FUNC(cal_panel_canvas_coord, int *, int *);
DECLARE_FAKE_VOID_FUNC(teleprompter_state_reset);
DECLARE_FAKE_VALUE_FUNC(bool, is_ble_device_bound);
/* GlassesState->imu.set_cb: sets the sample period */
DECLARE_FAKE_VOID_FUNC(g1_fake_imu_set, struct imu_ctx *, uint32_t);
/* get_msg_show_state() and set_msg_show_state() */
extern uint8_t g1_fake_msg_show_state;
/* get_message_by_index(); each index outside the array counts in g1_fake_msg_bad_index */
extern struct notif_msg g1_fake_msgs[];
extern int g1_fake_msg_bad_index;

/* g1_state(), set by the reset hook */
extern struct GlassesState *gs;
/* message_task.c keeps msg_timeout_flag static */
extern uint8_t *const msg_timeout_flag_ptr;

/* message_task.c's SCREEN_CHANGED and TASK_EXITED, and screen_ret while the handler runs */
#define RET_SCREEN_CHANGED 0x0b
#define RET_TASK_EXITED    (-1)
#define RET_NOT_RETURNED   0x7fff

/* Zephyr adds a tick to each relative sleep, so a 50 ms poll takes 51 ms. */
#define POLL_MS 51
/* a poll, then the master's 500 ms before a self exit */
#define SELF_EXIT_MS (POLL_MS + 501 + 5)

/* runs the handler of screen gs->dashboard_mode in the worker, from the test's next sleep */
void screen_start(void);
/* screen_start() with screen_task_dispatch() */
void dispatch_start(void);
/* advances the clock 1 ms at a time until the handler returned or max_ms passed */
bool screen_wait(uint32_t max_ms);
/* joins a handler that returned or aborts a running one; screen_after() calls it after each test */
void screen_stop(void);
/* ms since the start; ms from the start to the handler's return */
int64_t since_start(void);
int64_t done_after(void);
/* the screen started, what its handler returned, the uptime at the start */
extern int screen_started;
extern int screen_ret;
extern const char *screen_reason;
extern int64_t screen_start_ms;

/* trigger_screen_state_change() calls from the log as "who->N", plus " (no change)" when ignored */
const char *triggers(void);
/* sync_to_slave() calls, e.g. "0c 0b 00 00; 08" */
const char *syncs(void);

void head_up(void);
void head_down(void);
/* the screen is on and shows screen */
void showing(int screen);
/* the persist task record, also in the ESB sync package as threads.c copies it */
void persist_task(int task, int status);

/* the handler returns ret, and reason with RET_TASK_EXITED, within max_ms */
#define expect_exit_within(max_ms, ret, reason)                                                    \
  do {                                                                                       \
    bool _screen_returned = screen_wait(max_ms);                                       \
    zassert_true(_screen_returned, "screen %d handler still running after %d ms",      \
           screen_started, (int)since_start());                                  \
    zassert_equal(screen_ret, (ret), "screen %d handler returned %d, expected %d",     \
            screen_started, screen_ret, (ret));                                  \
    if ((ret) == RET_TASK_EXITED) {                                                    \
      expect_str(screen_reason, (reason));                                       \
    }                                                                                  \
  } while (0)

/* the handler has not returned after ms more */
#define expect_running_for(ms)                                                                     \
  do {                                                                                       \
    bool _screen_returned = screen_wait(ms);                                           \
    zassert_false(_screen_returned, "screen %d handler returned %d (%s) after %d ms",  \
            screen_started, screen_ret, screen_reason ? screen_reason : "-",     \
            (int)done_after());                                                  \
  } while (0)

/* starts the handler of the screen shown and expects it to keep running for ms */
#define start_screen_expect_running(ms)                                                            \
  do {                                                                                       \
    screen_start();                                                                    \
    expect_running_for(ms);                                                            \
  } while (0)

/* the task ended by itself: sync 8 closes the slave's screen and the screen is idle */
#define expect_task_closed(max_ms, reason)                                                         \
  do {                                                                                       \
    expect_exit_within((max_ms), RET_TASK_EXITED, (reason));                           \
    expect_str(syncs(), "08");                                                         \
    zassert_equal(gs->dashboard_mode, 0);                                              \
  } while (0)

/* sets work_mode (1 a local DFU, 8 one the case reports): the task ends at once, screen idle */
void expect_dfu_exit(int work_mode, const char *reason);
/* the app asks for the wait-for-head-down screen (1) while the task runs: the task ends */
void expect_wait_blow_head_exit(const char *reason);
/* the handler runs for ms and the screen stays */
void expect_screen_stays(uint32_t ms);

#endif /* G1_UNIT_SCREEN_TEST_H_ */
