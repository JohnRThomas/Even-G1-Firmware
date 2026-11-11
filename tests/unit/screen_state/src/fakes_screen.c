#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fakes_screen.h"
#include "g1_log.h"

#include "ble/ble_ancs.h"
#include "display/display_thread.h"
#include "notify/notify.h"
#include "storage/storage.h"
#include "threads/threads.h"
#include "ui/ui.h"

DEFINE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_glasses_ble_status);
DEFINE_FAKE_VOID_FUNC(report_event_to_phone, int);
__weak atomic_t dashboard_flags;
__weak uint8_t new_msg_signal_received;
struct g1_pkt_log g1_fake_sync_log;

DEFINE_FAKE_VALUE_FUNC(uint8_t, count_unread_messages);
DEFINE_FAKE_VALUE_FUNC(uint8_t, count_messages);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_msg_show_state);
DEFINE_FAKE_VOID_FUNC(set_msg_show_state, uint8_t);
DEFINE_FAKE_VALUE_FUNC(uint8_t, cur_message_show_state);
DEFINE_FAKE_VALUE_FUNC(uint8_t, g_arrMsgContent_count);
DEFINE_FAKE_VALUE_FUNC(struct notif_msg *, get_message_by_index, int);
DEFINE_FAKE_VOID_FUNC(clear_timeout_message, int);
DEFINE_FAKE_VOID_FUNC(mark_cur_message_read);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_msg_sync_state, int);
DEFINE_FAKE_VALUE_FUNC(bool, is_message_showing);
DEFINE_FAKE_VOID_FUNC(set_particle_pending);
uint8_t g1_fake_msg_show_state;
struct notif_msg g1_fake_msgs[G1_FAKE_MSG_NUM];
int g1_fake_msg_bad_index;
static struct notif_msg spare_msg;

DEFINE_FAKE_VALUE_FUNC(int, display_close_screen, int);
DEFINE_FAKE_VOID_FUNC(cal_panel_canvas_coord, int *, int *);
DEFINE_FAKE_VOID_FUNC(teleprompter_state_reset);
DEFINE_FAKE_VALUE_FUNC(bool, is_ble_device_bound);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_dashboard_startup_mode);

DEFINE_FAKE_VOID_FUNC(g1_fake_imu_set, struct imu_ctx *, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_panel_suspend, struct jbd_panel_context *);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_panel_resume, struct jbd_panel_context *);
struct g1_fake_panel_call g1_fake_panel_calls[G1_FAKE_PANEL_CALLS_MAX];
int g1_fake_panel_call_count;

__weak int BLE_DEBUG;
static char vlog[G1_FAKE_VLOG_MAX][G1_FAKE_VLOG_SIZE];
static int vlog_count;
static int vlog_dropped;

__weak void ble_printk(const char *fmt, ...)
{
	char buf[G1_FAKE_VLOG_SIZE];
	va_list ap;

	va_start(ap, fmt);
	memset(buf, 0, sizeof(buf));
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (vlog_count < G1_FAKE_VLOG_MAX) {
		memcpy(vlog[vlog_count++], buf, sizeof(buf));
	} else {
		vlog_dropped++;
	}
	printk("%s", buf);
}

int g1_fake_vlog_count(void)
{
	return vlog_count;
}

int g1_fake_vlog_dropped(void)
{
	return vlog_dropped;
}

const char *g1_fake_vlog_line(int i)
{
	return (i >= 0 && i < vlog_count) ? vlog[i] : NULL;
}

void g1_fake_vlog_clear(void)
{
	vlog_count = 0;
	vlog_dropped = 0;
}

static int sync_to_slave_default(struct GlassesState *gs, int cmd, void *data, size_t len)
{
	uint8_t pkt[1 + 4] = {(uint8_t)cmd};
	size_t n = (data && len) ? MIN(len, 4) : 0; /* sync_data holds 4 bytes */

	if (n) {
		memcpy(&pkt[1], data, n);
	}
	g1_pkt_log_add(&g1_fake_sync_log, pkt, 1 + n);
	gs->sync_ack = sync_to_slave_fake.return_val < 3000 ? 2 : 1;
	return sync_to_slave_fake.return_val;
}

static uint8_t get_msg_show_state_default(void)
{
	return g1_fake_msg_show_state;
}

static void set_msg_show_state_default(uint8_t val)
{
	g1_fake_msg_show_state = val;
}

static uint8_t g_arrMsgContent_count_default(void)
{
	return get_dashboard_state()->unread_msg_num;
}

static struct notif_msg *get_message_by_index_default(int idx)
{
	if (idx < 0 || idx >= G1_FAKE_MSG_NUM) {
		g1_fake_msg_bad_index++;
		memset(&spare_msg, 0, sizeof(spare_msg));
		return &spare_msg;
	}
	return &g1_fake_msgs[idx];
}

static void imu_set_default(struct imu_ctx *c, uint32_t val)
{
	if (c->sample_period_ms != val) {
		c->sample_period_ms = val;
	}
}

static void panel_call_log(char op)
{
	if (g1_fake_panel_call_count < G1_FAKE_PANEL_CALLS_MAX) {
		struct g1_fake_panel_call *c = &g1_fake_panel_calls[g1_fake_panel_call_count++];

		c->op = op;
		c->suspend_state = get_dashboard_state()->suspend_state;
		c->uptime_ms = k_uptime_get();
	}
}

static int panel_suspend_default(struct jbd_panel_context *c)
{
	ARG_UNUSED(c);
	panel_call_log('S');
	return 0;
}

static int panel_resume_default(struct jbd_panel_context *c)
{
	ARG_UNUSED(c);
	panel_call_log('R');
	return 0;
}

static void fakes_screen_reset(bool keep_media)
{
	struct GlassesState *gs = get_dashboard_state();

	ARG_UNUSED(keep_media);

	RESET_FAKE(sync_to_slave);
	sync_to_slave_fake.custom_fake = sync_to_slave_default;
	RESET_FAKE(sync_to_master);
	RESET_FAKE(get_glasses_ble_status);
	RESET_FAKE(report_event_to_phone);
	atomic_set(&dashboard_flags, 0);
	new_msg_signal_received = 0;
	g1_pkt_log_clear(&g1_fake_sync_log);

	RESET_FAKE(count_unread_messages);
	RESET_FAKE(count_messages);
	RESET_FAKE(get_msg_show_state);
	get_msg_show_state_fake.custom_fake = get_msg_show_state_default;
	RESET_FAKE(set_msg_show_state);
	set_msg_show_state_fake.custom_fake = set_msg_show_state_default;
	RESET_FAKE(cur_message_show_state);
	RESET_FAKE(g_arrMsgContent_count);
	g_arrMsgContent_count_fake.custom_fake = g_arrMsgContent_count_default;
	RESET_FAKE(get_message_by_index);
	get_message_by_index_fake.custom_fake = get_message_by_index_default;
	RESET_FAKE(clear_timeout_message);
	RESET_FAKE(mark_cur_message_read);
	RESET_FAKE(get_msg_sync_state);
	RESET_FAKE(is_message_showing);
	RESET_FAKE(set_particle_pending);
	g1_fake_msg_show_state = 0;
	memset(g1_fake_msgs, 0, sizeof(g1_fake_msgs));
	g1_fake_msg_bad_index = 0;

	RESET_FAKE(display_close_screen);
	RESET_FAKE(cal_panel_canvas_coord);
	RESET_FAKE(teleprompter_state_reset);
	RESET_FAKE(is_ble_device_bound);
	RESET_FAKE(get_dashboard_startup_mode);

	RESET_FAKE(g1_fake_imu_set);
	g1_fake_imu_set_fake.custom_fake = imu_set_default;
	RESET_FAKE(g1_fake_panel_suspend);
	g1_fake_panel_suspend_fake.custom_fake = panel_suspend_default;
	RESET_FAKE(g1_fake_panel_resume);
	g1_fake_panel_resume_fake.custom_fake = panel_resume_default;
	memset(g1_fake_panel_calls, 0, sizeof(g1_fake_panel_calls));
	g1_fake_panel_call_count = 0;

	gs->imu.set_cb = g1_fake_imu_set;
	gs->panel.suspend_cb = g1_fake_panel_suspend;
	gs->panel.resume_cb = g1_fake_panel_resume;

	/* the harness reset clears BLE_DEBUG before this hook */
	BLE_DEBUG = 1;
	g1_fake_vlog_clear();
}

G1_UNIT_RESET_HOOK(fakes_screen_reset);
