#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include "harness.h"
#include "g1_log.h"
#include "ble/ble_proto.h"
#include "notify/notify.h"
#include "notify/whitelist.h"
#include "storage/storage.h"
#include "utils/burial_point.h"

DEFINE_FFF_GLOBALS;

/* main.c is never linked; its globals live here. */
struct GlassesState *GLOBAL_STATE;
int LOG_LEVEL = 2;

struct GlassesState *get_dashboard_state(void)
{
	return GLOBAL_STATE;
}

struct ble_context *get_ble_context(void)
{
	return GLOBAL_STATE ? &GLOBAL_STATE->ble_context : NULL;
}

struct GlassesState *g1_state(void)
{
	return GLOBAL_STATE;
}

struct ble_context *g1_ble(void)
{
	return get_ble_context();
}

#define DT_DRV_COMPAT vnd_g1_unit_fake_device

static int g1_fake_device_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define G1_FAKE_DEVICE(inst)                                                                       \
	DEVICE_DT_INST_DEFINE(inst, g1_fake_device_init, NULL, NULL, NULL, POST_KERNEL, 0, NULL);

DT_INST_FOREACH_STATUS_OKAY(G1_FAKE_DEVICE)

/* Weak: NULL unless the suite links the defining file. */
void crc32_init(void);
int file_init(void);
int dequeue_file(void *out);
void ble_proto_test_reset(void);

#pragma weak ble_handle_rx_data
#pragma weak ble_proto_test_reset
#pragma weak BLE_DEBUG
#pragma weak app_list_init
#pragma weak load_app_white_list_from_flash
#pragma weak setting_msgq
#pragma weak quicknote_msgq
#pragma weak getAppLanguageInfofromFlash
#pragma weak getDashboardStartUpModeInfofromFlash
#pragma weak CURRENT_LANGUAGE
#pragma weak flash_ops_sem
#pragma weak LAST_FILE_CRC
#pragma weak sys_settings_dirty
#pragma weak crc32_init
#pragma weak file_init
#pragma weak dequeue_file
#pragma weak SERIAL_NUMBER
#pragma weak glasses_name
#pragma weak init_boot_seconds
#pragma weak set_wall_clock_ms

extern struct k_msgq setting_msgq;
extern struct k_msgq quicknote_msgq;

struct g1_frame {
	uint16_t len;
	uint16_t sent;    /* length passed to write() */
	uint8_t data[G1_FRAME_SIZE];
};

static struct g1_frame frames[G1_FRAME_MAX];
static int frame_head;
static int frame_count;
static int frames_dropped;
int g1_write_ret;

static int g1_capture_write(const uint8_t *data, uint16_t len)
{
	struct g1_frame *f;

	if (frame_count == G1_FRAME_MAX) {
		frames_dropped++;
		return g1_write_ret;
	}
	f = &frames[(frame_head + frame_count) % G1_FRAME_MAX];
	f->sent = len;
	f->len = MIN(len, G1_FRAME_SIZE);
	memcpy(f->data, data, f->len);
	frame_count++;
	return g1_write_ret;
}

int g1_frame_count(void)
{
	return frame_count;
}

int g1_frame_pop(uint8_t *buf, size_t cap)
{
	struct g1_frame *f;

	if (frame_count == 0) {
		return -ENOMSG;
	}
	f = &frames[frame_head];
	memcpy(buf, f->data, MIN(cap, f->len));
	frame_head = (frame_head + 1) % G1_FRAME_MAX;
	frame_count--;
	return f->len;
}

const uint8_t *g1_frame_peek(int i, size_t *len)
{
	struct g1_frame *f;

	if (i < 0 || i >= frame_count) {
		return NULL;
	}
	f = &frames[(frame_head + i) % G1_FRAME_MAX];
	if (len) {
		*len = f->len;
	}
	return f->data;
}

void g1_frames_clear(void)
{
	frame_head = 0;
	frame_count = 0;
	frames_dropped = 0;
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static int hex_parse_range(const char *p, const char *end, uint8_t *buf, size_t cap)
{
	size_t n = 0;

	while (p < end) {
		if (*p == '#') {
			while (p < end && *p != '\n') {
				p++;
			}
			continue;
		}
		if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',' || *p == ':') {
			p++;
			continue;
		}
		if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && hexval(p[2]) >= 0 &&
		    hexval(p[3]) >= 0) {
			p += 2;
		}
		if (hexval(p[0]) < 0 || hexval(p[1]) < 0) {
			return -EINVAL;
		}
		if (n == cap) {
			return -ENOSPC;
		}
		buf[n++] = (uint8_t)(hexval(p[0]) << 4 | hexval(p[1]));
		p += 2;
	}
	return (int)n;
}

int g1_hex_parse(const char *text, uint8_t *buf, size_t cap)
{
	return hex_parse_range(text, text + strlen(text), buf, cap);
}

void g1_hex_format(const uint8_t *buf, size_t len, char *out, size_t out_size)
{
	size_t o = 0;

	if (out_size == 0) {
		return;
	}
	out[0] = '\0';
	for (size_t i = 0; i < len; i++) {
		if (o + 8 > out_size) {
			snprintf(out + o, out_size - o, "...");
			return;
		}
		o += snprintf(out + o, out_size - o, i ? " %02x" : "%02x", buf[i]);
	}
}

extern const char *const g1_vector_files[];
extern const size_t g1_vector_file_count;

static const char *next_line(const char *p)
{
	const char *nl = strchr(p, '\n');

	return nl ? nl + 1 : p + strlen(p);
}

/* True if the line is the title "# <tag>" */
static bool is_vector_title(const char *line, const char *tag, size_t len)
{
	if (*line++ != '#') {
		return false;
	}
	while (*line == ' ' || *line == '\t') {
		line++;
	}
	if (strncmp(line, tag, len) != 0) {
		return false;
	}
	line += len;
	while (*line == ' ' || *line == '\t' || *line == '\r') {
		line++;
	}
	return *line == '\n' || *line == '\0';
}

/* The first byte of the hex text at p, or -1 */
static int first_byte(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
		p++;
	}
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
	}
	if (hexval(p[0]) < 0 || hexval(p[1]) < 0) {
		return -1;
	}
	return hexval(p[0]) << 4 | hexval(p[1]);
}

/*
 * "0x<id>_<tag>" is the packet titled "# <tag>" whose first byte is <id>. A packet is the data
 * lines between its title and the next comment line.
 */
int g1_vector_load(const char *name, uint8_t *buf, size_t cap)
{
	if (name[0] != '0' || name[1] != 'x' || hexval(name[2]) < 0 || hexval(name[3]) < 0 ||
	    name[4] != '_' || name[5] == '\0') {
		return -ENOENT;
	}
	int id = hexval(name[2]) << 4 | hexval(name[3]);
	const char *tag = name + 5;
	size_t len = strlen(tag);

	for (size_t i = 0; i < g1_vector_file_count; i++) {
		for (const char *line = g1_vector_files[i]; *line; line = next_line(line)) {
			if (!is_vector_title(line, tag, len) || first_byte(next_line(line)) != id) {
				continue;
			}
			const char *data = next_line(line);
			const char *end = data;

			while (*end && *end != '#') {
				end = next_line(end);
			}
			return hex_parse_range(data, end, buf, cap);
		}
	}
	return -ENOENT;
}

static uint8_t expected[G1_FRAME_SIZE];

bool g1_frame_check(const char *hex, int pad_to, char *msg, size_t msg_size)
{
	static char exp_txt[G1_MSG_SIZE / 2 - 64];
	static char got_txt[G1_MSG_SIZE / 2 - 64];
	uint8_t got[G1_FRAME_SIZE];
	size_t first_diff;
	int n, len;

	n = g1_hex_parse(hex, expected, sizeof(expected));
	if (n < 0) {
		snprintf(msg, msg_size, "bad expected hex \"%s\" (%d)", hex, n);
		return false;
	}
	if (pad_to >= 0) {
		if (n > pad_to || pad_to > (int)sizeof(expected)) {
			snprintf(msg, msg_size, "expected hex has %d bytes, more than the padded %d",
				 n, pad_to);
			return false;
		}
		memset(expected + n, 0, pad_to - n);
		n = pad_to;
	}
	g1_hex_format(expected, n, exp_txt, sizeof(exp_txt));
	if (frames_dropped) {
		snprintf(msg, msg_size, "frame queue overflowed (%d frames dropped) before %s",
			 frames_dropped, exp_txt);
		return false;
	}
	if (frame_count == 0) {
		snprintf(msg, msg_size, "no frame sent; expected (%d bytes) %s", n, exp_txt);
		return false;
	}
	if (frames[frame_head].sent > G1_FRAME_SIZE) {
		snprintf(msg, msg_size, "frame of %u bytes is longer than G1_FRAME_SIZE",
			 frames[frame_head].sent);
		return false;
	}
	len = g1_frame_pop(got, sizeof(got));
	g1_hex_format(got, len, got_txt, sizeof(got_txt));
	if (len == n && memcmp(got, expected, n) == 0) {
		return true;
	}
	for (first_diff = 0; first_diff < (size_t)MIN(len, n); first_diff++) {
		if (got[first_diff] != expected[first_diff]) {
			break;
		}
	}
	snprintf(msg, msg_size,
		 "frame differs at byte %u\n  expected (%d bytes) %s\n  got      (%d bytes) %s",
		 (unsigned int)first_diff, n, exp_txt, len, got_txt);
	return false;
}

bool g1_frame_check_none(char *msg, size_t msg_size)
{
	char txt[256];
	size_t len;
	const uint8_t *f;

	if (frame_count == 0 && frames_dropped == 0) {
		return true;
	}
	f = g1_frame_peek(0, &len);
	if (f == NULL) {
		snprintf(msg, msg_size, "expected no frame, %d were dropped", frames_dropped);
		return false;
	}
	g1_hex_format(f, len, txt, sizeof(txt));
	snprintf(msg, msg_size, "expected no frame, %d queued; first (%u bytes) %s", frame_count,
		 (unsigned int)len, txt);
	return false;
}

void g1_pkt_log_add(struct g1_pkt_log *log, const void *data, size_t len)
{
	if (log->count == G1_PKT_LOG_MAX) {
		log->dropped++;
		return;
	}
	log->len[log->count] = len;
	memcpy(log->data[log->count], data, MIN(len, G1_PKT_LOG_SIZE));
	log->count++;
}

void g1_pkt_log_clear(struct g1_pkt_log *log)
{
	log->count = 0;
	log->dropped = 0;
}

void g1_send(const uint8_t *buf, size_t len)
{
	struct ble_context *ctx = g1_ble();

	zassert_not_null(ble_handle_rx_data,
			 "g1_send needs ble/ble_proto.c: g1_unit_firmware(ble_proto) or (protocol)");
	/* As ble_work_thread(): short packets reuse rx_bufs[2] without clearing it. */
	zassert_true(len <= sizeof(ctx->rx_data), "packet of %u bytes: NUS rx holds %u",
		     (unsigned int)len, (unsigned int)sizeof(ctx->rx_data));
	if (len > 20) {
		memcpy(ctx->rx_data, buf, len);
		ctx->rx_len = len;
		ble_handle_rx_data(ctx->rx_data, len, 0);
		memset(ctx->rx_data, 0, sizeof(ctx->rx_data));
	} else {
		memcpy(ctx->rx_bufs[2], buf, len);
		ctx->rx_pending = 1;
		ctx->small_len = len;
		ble_handle_rx_data(ctx->rx_bufs[2], len, 0);
	}
	ctx->rx_len = 0;
	ctx->rx_pending = 0;
}

void g1_send_hex(const char *hex)
{
	uint8_t buf[256];
	int n = g1_hex_parse(hex, buf, sizeof(buf));

	zassert_true(n >= 0, "bad hex \"%s\" (%d)", hex, n);
	g1_send(buf, n);
}

void g1_send_vector(const char *name)
{
	uint8_t buf[256];
	int n = g1_vector_load(name, buf, sizeof(buf));

	zassert_true(n >= 0, "vector %s: %d (tests/cases/*.hex)", name, n);
	g1_send(buf, n);
}

void g1_clock_advance_ms(uint32_t ms)
{
	k_sleep(K_TIMEOUT_ABS_MS(k_uptime_get() + ms));
}

int64_t g1_clock_uptime_ms(void)
{
	return k_uptime_get();
}

void g1_clock_set_date(uint32_t unix_s)
{
	GLOBAL_STATE->date_ts = unix_s;
}

void g1_clock_set_wall_ms(int64_t ms)
{
	zassert_not_null(set_wall_clock_ms, "g1_clock_set_wall_ms needs utils/common_utils.c");
	set_wall_clock_ms(ms);
}

/* Fixed address: firmware keeps pointers into it. */
static struct GlassesState state_storage;

#define G1_BLOCKS(X)                                                                               \
	X(dashboard_ts, 0x75)                                                                      \
	X(countdown_ts, 7)                                                                         \
	X(task_status, 5)                                                                          \
	X(teleprompter, 0x217)                                                                     \
	X(nav, 0xf5)                                                                               \
	X(translate_src, 0x1e8)                                                                    \
	X(translate_dst, 0x1e8)                                                                    \
	X(dash_buf, 0x1e9)                                                                         \
	X(evenai_buf, 0x210)                                                                       \
	X(quicknote, 0x50f)                                                                        \
	X(quicknote_show_info, 0x152)                                                              \
	X(esb_up_snapshot, 0x201)                                                                  \
	X(esb_down_snapshot, 0x201)                                                                \
	X(user_sched_info, 0x16a)                                                                  \
	X(schedule_notif, 0xaf)                                                                    \
	X(bitmap_info, 0xb)                                                                        \
	X(onboarding, 0x21)                                                                        \
	X(raster_height_cfg, 1)                                                                    \
	X(horiz_pitch_cfg, 2)                                                                      \
	X(ts_info, 10)                                                                             \
	X(transcribe_buf, 0x208)

static void *block_alloc(size_t size)
{
	void *p = malloc(size);

	__ASSERT(p != NULL, "harness: out of heap (CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE)");
	memset(p, 0, size);
	return p;
}

static void state_free(struct GlassesState *s)
{
#define G1_FREE(field, size) free(s->field);
	G1_BLOCKS(G1_FREE)
#undef G1_FREE
	free(s->ble_context.tx_buf);
	free(s->ble_context.small_buf);
}

static void ble_context_setup(struct ble_context *ctx)
{
	ctx->large_buf_0 = (uint8_t *)ctx + 0x394;
	ctx->write = g1_capture_write;
	k_sem_init(&ctx->rx_sem, 0, 1);
	k_sem_init(&ctx->tx_sem, 0, 2);
	ctx->large_buf_1 = (uint8_t *)ctx + 0x8d4;
	ctx->connected = 0;
	ctx->send_enabled = 0;
	ctx->restart_adv = 0;
	ctx->small_buf = block_alloc(21);
	ctx->tx_buf = block_alloc(BLE_TX_BUF_SIZE);
}

static void state_build(void)
{
	struct GlassesState *s = &state_storage;

	if (GLOBAL_STATE == s) {
		state_free(s);
	}
	/* init_context() minus hardware and flash */
	memset(s, 0, sizeof(*s));
	memset(s->master_mac, 0xff, sizeof(s->master_mac));
	memset(s->slave_mac, 0xff, sizeof(s->slave_mac));
	GLOBAL_STATE = s;
	s->is_master = 1;

	k_sem_init(&s->ipc_sem, 0, 1);
	k_sem_init(&s->sem_1, 0, 10);
	k_sem_init(&s->sem_2, 0, 1);
	k_sem_init(&s->screen_on_sem, 0, 1);
	k_sem_init(&s->imu_sem, 0, 1);
	k_sem_init(&s->main_sem, 0, 1);
	k_sem_init(&s->dashboard_position_sem, 0, 1);
	k_sem_init(&s->sem_7, 0, 1);
	k_sem_init(&s->touch_sem, 0, 1);

	/* change_work_mode(0) from 0xff */
	s->work_mode = 0;
	s->module_mask = 0x0dfb;

#define G1_ALLOC(field, size) s->field = block_alloc(size);
	G1_BLOCKS(G1_ALLOC)
#undef G1_ALLOC
	s->date_ts = G1_RESET_DATE;

	memset(&s->bonded_addr, 0xff, sizeof(s->bonded_addr));
	s->bond_flags = 0;

	s->spi.is_initialized = 0;
	s->spi.pins[0] = 4;
	s->spi.pins[1] = 8;
	s->spi.pins[2] = 9;
	s->spi.pins[3] = 10;
	s->spi.ss = 11;
	s->spi.speed_mhz = 0x20;

	s->master_fw_ver.major = 1;
	s->master_fw_ver.minor = 6;
	s->master_fw_ver.patch = 6;
	s->sync.flags |= BIT(2);

	ble_context_setup(&s->ble_context);

	s->user_data_reset_pending = 0;
	s->panel.brightness_max = s->panel.lum_gear;
	s->mic_nocompress = 0;
}

void g1_unit_set_role(int role)
{
	struct GlassesState *s = GLOBAL_STATE;

	zassert_true(role == G1_ROLE_MASTER || role == G1_ROLE_SLAVE, "role %d", role);
	memset(&s->master_fw_ver, 0, sizeof(s->master_fw_ver));
	memset(&s->fw_ver, 0, sizeof(s->fw_ver));
	s->sync.flags &= ~BIT(2);
	s->esb_pkt.flags &= ~BIT(2);
	s->is_master = role;
	if (role == G1_ROLE_MASTER) {
		s->master_fw_ver.major = 1;
		s->master_fw_ver.minor = 6;
		s->master_fw_ver.patch = 6;
		s->sync.flags |= BIT(2);
	} else {
		s->fw_ver.major = 1;
		s->fw_ver.minor = 6;
		s->fw_ver.patch = 6;
		s->esb_pkt.flags |= BIT(2);
	}
}

#define G1_RESET_HOOKS_MAX 32

static g1_unit_reset_fn reset_hooks[G1_RESET_HOOKS_MAX];
static int reset_hook_count;

void g1_unit_register_reset(g1_unit_reset_fn fn)
{
	__ASSERT(reset_hook_count < G1_RESET_HOOKS_MAX, "harness: too many reset hooks");
	reset_hooks[reset_hook_count++] = fn;
}

static void run_hooks(bool keep_media)
{
	for (int i = 0; i < reset_hook_count; i++) {
		reset_hooks[i](keep_media);
	}
}

/* no public prototype */
void __printk_hook_install(int (*fn)(int c));
void *__printk_get_hook(void);

static int quiet_char_out(int c)
{
	return c;
}

static bool snapshot_taken;
static char serial_number_init[32];
static char glasses_name_init[32];
static bool file_queue_ready;

static void firmware_reset(void)
{
	struct GlassesState *s = GLOBAL_STATE;

	if (!snapshot_taken) {
		if (SERIAL_NUMBER) {
			memcpy(serial_number_init, SERIAL_NUMBER, sizeof(serial_number_init));
		}
		if (glasses_name) {
			memcpy(glasses_name_init, glasses_name, sizeof(glasses_name_init));
		}
		snapshot_taken = true;
	}
	if (SERIAL_NUMBER) {
		memcpy(SERIAL_NUMBER, serial_number_init, sizeof(serial_number_init));
	}
	if (glasses_name) {
		memcpy(glasses_name, glasses_name_init, sizeof(glasses_name_init));
	}

	if (ble_proto_test_reset) {
		ble_proto_test_reset();
	}
	if (&BLE_DEBUG) {
		BLE_DEBUG = 0;
	}

	if (&flash_ops_sem) {
		k_sem_init(&flash_ops_sem, 0, 20);
	}
	if (crc32_init) {
		crc32_init();
	}
	if (&LAST_FILE_CRC) {
		LAST_FILE_CRC = 0;
	}
	if (&sys_settings_dirty) {
		sys_settings_dirty = 0;
	}
	if (file_init && dequeue_file) {
		uint8_t msg[256];

		if (!file_queue_ready) {
			file_init();
			file_queue_ready = true;
		}
		while (dequeue_file(msg) == 0) {
		}
	}

	/* Purged directly: clearSettingStorageQueue() is unreliable. */
	if (&setting_msgq) {
		k_msgq_purge(&setting_msgq);
	}
	if (&quicknote_msgq) {
		k_msgq_purge(&quicknote_msgq);
	}

	if (&CURRENT_LANGUAGE) {
		memset((void *)&CURRENT_LANGUAGE, 0, sizeof(CURRENT_LANGUAGE));
	}
	if (s->flash_read) {
		if (getDashboardStartUpModeInfofromFlash) {
			getDashboardStartUpModeInfofromFlash();
		}
		if (getAppLanguageInfofromFlash) {
			getAppLanguageInfofromFlash();
		}
		if (app_list_init) {
			app_list_init();
		} else if (load_app_white_list_from_flash) {
			load_app_white_list_from_flash();
		}
	}
}

static void clock_reset(void)
{
	int64_t now = k_uptime_get();

	/* whole second, so s = ms / 1000 is exact */
	if (now % 1000) {
		k_sleep(K_TIMEOUT_ABS_MS(now - now % 1000 + 1000));
	}
	if (init_boot_seconds) {
		init_boot_seconds();
	}
	if (set_wall_clock_ms) {
		set_wall_clock_ms(0);
	}
	GLOBAL_STATE->date_ts = G1_RESET_DATE;
}

static void reset(bool keep_media)
{
	void *hook = __printk_get_hook();

	__printk_hook_install(quiet_char_out);

	state_build();
	run_hooks(keep_media);
	firmware_reset();
	run_hooks(true);
	g1_frames_clear();
	g1_write_ret = 0;
	LOG_LEVEL = 2;
	FFF_RESET_HISTORY();

	__printk_hook_install(hook);
	clock_reset();
}

void g1_unit_reset(void)
{
	reset(false);
}

void g1_unit_reboot(void)
{
	reset(true);
}

static void g1_unit_before_each(const struct ztest_unit_test *test, void *fixture)
{
	ARG_UNUSED(test);
	ARG_UNUSED(fixture);
	g1_unit_reset();
}

ZTEST_RULE(g1_unit_harness, g1_unit_before_each, NULL);
