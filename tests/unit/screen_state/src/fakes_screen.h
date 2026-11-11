/*
 * Weak fakes for message_task.c's dependencies. sync_to_slave logs (cmd, data) and sets sync_ack to
 * 2 when return_val < 3000 ms, else 1.
 */
#ifndef G1_UNIT_FAKES_SCREEN_H_
#define G1_UNIT_FAKES_SCREEN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

#include "harness.h"

DECLARE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_glasses_ble_status);
DECLARE_FAKE_VOID_FUNC(report_event_to_phone, int);
extern atomic_t dashboard_flags;
extern uint8_t new_msg_signal_received;
/* byte 0 = cmd, then data */
extern struct g1_pkt_log g1_fake_sync_log;

#define G1_FAKE_MSG_NUM 20 /* MSG_TOTAL_NUM */
DECLARE_FAKE_VALUE_FUNC(uint8_t, count_unread_messages);
DECLARE_FAKE_VALUE_FUNC(uint8_t, count_messages);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_msg_show_state);
DECLARE_FAKE_VOID_FUNC(set_msg_show_state, uint8_t);
DECLARE_FAKE_VALUE_FUNC(uint8_t, cur_message_show_state);
DECLARE_FAKE_VALUE_FUNC(uint8_t, g_arrMsgContent_count);
DECLARE_FAKE_VALUE_FUNC(struct notif_msg *, get_message_by_index, int);
DECLARE_FAKE_VOID_FUNC(clear_timeout_message, int);
DECLARE_FAKE_VOID_FUNC(mark_cur_message_read);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_msg_sync_state, int);
DECLARE_FAKE_VALUE_FUNC(bool, is_message_showing);
extern uint8_t g1_fake_msg_show_state;
extern struct notif_msg g1_fake_msgs[G1_FAKE_MSG_NUM];
extern int g1_fake_msg_bad_index;

DECLARE_FAKE_VOID_FUNC(set_particle_pending);

DECLARE_FAKE_VALUE_FUNC(int, display_close_screen, int);

DECLARE_FAKE_VOID_FUNC(cal_panel_canvas_coord, int *, int *);
DECLARE_FAKE_VOID_FUNC(teleprompter_state_reset);

DECLARE_FAKE_VALUE_FUNC(bool, is_ble_device_bound);

DECLARE_FAKE_VALUE_FUNC(uint8_t, get_dashboard_startup_mode);

DECLARE_FAKE_VOID_FUNC(g1_fake_imu_set, struct imu_ctx *, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_panel_suspend, struct jbd_panel_context *);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_panel_resume, struct jbd_panel_context *);

#define G1_FAKE_PANEL_CALLS_MAX 8
struct g1_fake_panel_call {
	char op;               /* 'S' suspend, 'R' resume */
	uint8_t suspend_state;
	int64_t uptime_ms;
};
extern struct g1_fake_panel_call g1_fake_panel_calls[G1_FAKE_PANEL_CALLS_MAX];
extern int g1_fake_panel_call_count;

#define G1_FAKE_VLOG_MAX  256
#define G1_FAKE_VLOG_SIZE 200 /* DEBUG_MSG_SIZE */
extern int BLE_DEBUG;
void ble_printk(const char *fmt, ...);
int g1_fake_vlog_count(void);
int g1_fake_vlog_dropped(void);
const char *g1_fake_vlog_line(int i);
void g1_fake_vlog_clear(void);

#endif /* G1_UNIT_FAKES_SCREEN_H_ */
