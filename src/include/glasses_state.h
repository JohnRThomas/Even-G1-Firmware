/*
 * Global state (GLOBAL_STATE / get_dashboard_state()) and the
 * sub-structs reached through pointers into it (get_ble_context(),
 * get_jdb_panel_context(), ...).
 *
 * Anonymous unions are used where the code reads one location two ways
 * (whole packet vs. single bytes, array vs. named slots).
 */
#ifndef G1_GLASSES_STATE_H_
#define G1_GLASSES_STATE_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/addr.h>
#include <nrfx_spim.h>
#include "../ahrs/ahrs.h"

struct GlassesState;

/* pointees of the pointer block, defined by their owners */
struct schedule_notif;

/* ------------------------------------------------------------------ ESB */

/* first byte of every ESB package */
struct esb_hdr {
	uint8_t cmd: 6;
	uint8_t dir: 2;
};

struct msg_status {
	uint32_t task;
	uint8_t status;
} __packed;

#define FW_VER_MAJOR 1
#define FW_VER_MINOR 6
#define FW_VER_PATCH 7

struct fw_ver {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
};

/* sync package sent by the master */
struct esb_sync_pkt {
	struct esb_hdr hdr;
	uint8_t screen_id;                       /* dashboard_mode */
	uint16_t canvas_x;
	uint16_t canvas_y;
	uint8_t package_id;
	uint8_t stay_time_left_s;
	uint8_t unknown_task_count;
	uint8_t unread_msg_num;
	uint8_t battery_min;
	struct msg_status status;
	uint8_t flags;
	uint8_t arg[4];
} __packed;

/* peer status package */
struct esb_pkt {
	uint8_t cmd;                             /* cmd | 0x80 */
	struct fw_ver ver;
	uint8_t battery;
	uint8_t f_05[7];
	uint8_t mac[6];
	uint8_t flags;
	union {
		uint8_t data[2];
		struct {
			uint8_t subcmd;
			uint8_t value;
		};
	};
	uint8_t seq;
} __packed;

/* reception buffer, read per page */
struct db_info {
	uint8_t pad_00[4];
	uint8_t weather_icon;                    /* 0 / 0xff = none */
	int8_t temperature;                      /* Celsius */
	uint8_t pad_06;
	uint8_t new_msg_count;
	uint8_t pad_08[0x57];
	uint8_t date_format;                     /* 0 = mm/dd, 1 = dd-mm, else dd/mm */
	uint8_t low_power;                            /* 1 = show the 0x42 icon */
	uint8_t pad_61[4];
	uint8_t display_mode;
	uint8_t pane_mode;
	uint8_t calendar_disp_mode;
	uint8_t pad_68;
	uint8_t stocks_disp_mode;
	uint8_t news_disp_mode;
	uint8_t citywalk_disp_mode;
	uint8_t pad_6c;
	uint16_t citywalk_x;
	uint16_t citywalk_y;
	uint8_t citywalk_state;
	uint8_t pad_72;
	uint8_t dashboard_on;
} __packed;

/* screen payload buffer, read per page */
union screen_payload {
	uint8_t buf[0x5dc];
	struct db_info db;                       /* dashboard */
	struct {
		float yaw;
		float pitch;
		float roll;
	} __packed ypr;                          /* gyro template / imu tx */
	struct ui_timer_payload {
		uint32_t seconds;
		uint8_t state;
	} __packed timer;                        /* template timer screen */
	struct {
		uint8_t pad[7];
		uint8_t count;
	} __packed new_msg;
	struct {
		uint8_t pad_00;
		uint8_t ui_action;               /* page action / step */
		uint8_t show_clock;
		uint8_t ui_status;               /* page sub-state (even_ai_v2) */
		uint8_t page_flags;              /* bit0: animated icon */
		uint8_t dark_cnt;
		uint8_t pad_06[2];
		char text[1];
		uint8_t pad_09[7];
		char ai_text[1];                 /* even_ai_v2 text */
	} __packed gui;                          /* gui pages */
	struct {
		uint8_t longpress_req;
		uint8_t sub_mode;
		uint8_t page;
		uint8_t status;
		uint8_t mode;
		uint8_t scroll_line;
		uint8_t pad_06[2];
		char text[0x5d4];
	} __packed qn;                           /* quicknote */
	struct {
		uint8_t status;
		uint8_t sub_status;              /* read under screen_payload_mutex */
		uint8_t pad_02[2];
		uint8_t percent;
		uint8_t hide_bar;
		uint8_t page;
		char text[0x1e0];
		uint16_t text_len;
	} __packed ai;                           /* even AI */
	struct {
		uint8_t work_mode;
		uint8_t cmd;
		uint8_t pad_02[8];
		uint8_t status;
		uint8_t pad_0b;
		int8_t countdown;
		uint8_t percent;
		uint16_t dark_lines;
		uint8_t suspend;
		int32_t suspend_time;
		uint8_t pad_15[2];
		char text[0x200];
	} __packed tp;                           /* teleprompter */
	struct trans_info {
		uint8_t translation_type;        /* 1-based language index */
		uint8_t original_type;
		uint8_t suspend_en;
		uint8_t mode;
		uint8_t pad_04[2];
		char text[0x1e0];
	} __packed trans;                        /* translate (written by the BLE handler) */
	struct nav_page {
		uint8_t running;
		uint8_t direction;
		uint8_t pad_02[5];
		uint8_t imu_action_status;
		uint16_t pos_x;                  /* position on the panoramic map */
		uint16_t pos_y;
		uint8_t pad_0c;
		char total_time[0x18];
		char total_distance[0x18];
		char road[0x40];
		char distance[0x18];
		char speed[0x1c];
		uint32_t map_size;               /* overview map size (0x1210 = raw) */
	} __packed nav;                          /* navigation (struct nav_page) */
	struct {
		uint8_t pad_00[0xae];
		uint8_t status;                  /* 1 arriving, 2 arrived */
		uint8_t pad_af[6];
		char text[0x527];
	} __packed nav_arrived;                  /* navigation, arrival page */
} __packed;

/* GlassesState->dash_buf: dashboard data received with cmd 0x4e */
#define DASH_DATA_LEN 0x1e0

struct dash_data_buf {
	uint8_t type;
	uint8_t screen_status;
	uint16_t char_pos;                            /* big endian in the packet */
	uint8_t scroll_pct;
	uint8_t page_count;
	uint8_t seq;
	uint8_t data[DASH_DATA_LEN];
	uint16_t len;
} __packed;

/* GlassesState->evenai_buf (cmd 0x54) */
struct evenai_buf {
	uint8_t state;
	uint8_t sub;
	uint8_t wakeup_arg;
	uint8_t action_status;
	uint8_t scroll_line;
	uint8_t scroll_pct;
	int64_t ts;
	uint16_t len;
	uint8_t data[0x200];
} __packed;

/* GlassesState->transcribe_buf (cmd 0x52 / 0x53) */
struct transcribe_buf {
	bool running;
	uint8_t f_1;
	uint8_t mode;
	uint8_t status;
	uint8_t flags;
	uint8_t dark_lines;
	uint16_t len;
	uint8_t data[0x200];
};

/* GlassesState->quicknote_show_info */
struct quicknote_show_info {
	uint8_t longpress_req;
	uint8_t enable;
	uint8_t started_by_app;
	uint8_t action;
	uint8_t result_code;
	uint8_t scroll_line;
	uint16_t len;
	uint8_t data[0x14a];
};

/* ------------------------------------------------------------------ IPC */

struct ipc_ep_cfg {
	uint8_t id;
	const char *name;
	int (*recv_cb)(struct GlassesState *gs, const uint8_t *data, size_t len);
};

#define IPC_RECEIVER_MAX 22

/* filled by ipc_host.c */
struct ipc_service_context {
	uint32_t f_00;
	int count;
	struct ipc_ep_cfg *receivers[IPC_RECEIVER_MAX];
	int (*send)(const void *data, size_t len);
	int (*register_recv_cb)(struct ipc_ep_cfg *ep);
};

/* ------------------------------------------------------------------ BLE */

/*
 * One notification: the ANCS record being received
 * (ble_context.notif) and the entries of g_arrMsgContent (notification.c).
 */
struct notif_msg {
	uint32_t recv_time;
	uint32_t start_show_time;
	uint32_t uid;                            /* uidTail */
	uint8_t pos;
	uint8_t sync_state;                      /* master/slave display sync, 0..4 */
	uint8_t busy;
	uint8_t read: 1;                        /* bit 0 */
	uint8_t state: 7;                       /* bits 1..7 */
	char app_identifier[32];
	char display_name[32];
	char title[32];
	char subtitle[32];
	char message[256];
	char date[32];
	uint32_t action;
};

/* the master/slave ESB sync sends a notification as two 0xda-byte halves */
#define NOTIF_MSG_HALF (sizeof(struct notif_msg) / 2)

struct ble_context {
	int (*init_cb)(struct ble_context *ctx);
	int (*write)(const uint8_t *data, uint16_t len);
	uint8_t *tx_buf;                         /* malloc(0x2b8) */
	uint32_t tx_len;
	uint8_t charging;
	struct notif_msg notif;
	struct k_sem ancs_get_attr_data_sem;
	struct k_sem ancs_get_attr_req_sem;
	struct k_sem rx_sem;
	struct k_sem tx_sem;
	uint8_t receiver_thd_is_running;
	uint8_t *small_buf;                      /* packets of up to BLE_SMALL_PKT_MAX bytes */
	uint8_t rx_data[0x100];
	uint32_t rx_len;
	uint32_t rx_pending;
	uint32_t small_len;
	uint8_t restart_adv;
	uint8_t connected;
	uint8_t send_enabled;
	uint8_t security_pending;
	uint32_t last_rx_time;
	uint32_t connected_time;
};

/* ------------------------------------------------------------------ SPI / panel */

/* spi_interface_t */
struct spi_master_ctx {
	int (*init_cb)(struct spi_master_ctx *ctx);
	int (*deinit_cb)(struct spi_master_ctx *ctx);
	int (*xfer_cb)(struct spi_master_ctx *ctx, const uint8_t *tx, size_t tx_len, uint8_t *rx,
		       size_t rx_len);
	nrfx_spim_t spim;
	uint32_t is_initialized;
	union {
		uint8_t pins[4];
		struct {
			uint8_t bus;             /* 3 or 4 */
			uint8_t sck;
			uint8_t mosi;
			uint8_t miso;
		};
	};
	uint8_t ss;
	uint8_t speed_mhz;
	uint8_t xfer_done;
};

struct flash_ops {
	uint32_t f_0;
	void (*fn)(struct flash_ops *ops);
};

#define PANEL_FB_ROWS 200

struct jbd_panel_context;
typedef int (*panel_cb_t)(struct jbd_panel_context *c);

struct jbd_panel_context {
	panel_cb_t init_cb;
	panel_cb_t suspend_cb;
	panel_cb_t resume_cb;
	panel_cb_t off_cb;
	panel_cb_t on_cb;
	int (*default_task)(struct jbd_panel_context *c, int screen_id, void *ctx);
	void (*draw_image_cb)(struct jbd_panel_context *c);
	void *(*clear_cb)(struct jbd_panel_context *c);
	int (*set_brightness_cb)(struct jbd_panel_context *c, uint8_t level);
	uint8_t *panel_buffer[PANEL_FB_ROWS];    /* frame buffer row pointers */
	uint8_t *current_buffer;
	uint32_t current_row;
	uint32_t current_column;
	uint32_t current_buffer_size;
	uint8_t raster_height_offset;
	uint8_t canvas_distance_gear;
	uint32_t y_offset;
	uint32_t init_done;
	uint32_t f_360;
	uint8_t voltage;
	uint8_t panel_brightness_level;
	uint8_t last_synced_brightness;
	uint8_t applied_brightness;
	union {
		uint16_t brightness_lum[4];
		struct {
			uint16_t brightness_max;
			uint16_t lum_base_0;
			uint16_t lum_base_1;
			uint16_t lum_gear;
		};
	};
	uint8_t brightness_percent;
	struct spi_master_ctx *spi_callbacks;
};

/* ------------------------------------------------------------------ IMU / light */

/* imu_fusion_context */
struct imu_ctx {
	uint8_t mode;
	uint32_t on;
	int thr_low;
	int thr_high;
	uint8_t level;                           /* wakeup angle */
	uint8_t wear;
	uint8_t inited;
	uint8_t thread_running;
	uint32_t sample_period_ms;
	const struct device *dev;
	struct sensor_value odr;
	struct ahrs fusion;
	uint8_t dof3_enable;
	int ref;
	int base;                                /* user horizontal pitch */
	int div;
	int (*init_cb)(struct imu_ctx *c);
	int (*deinit_cb)(struct imu_ctx *c);
	int (*get_cb)(struct imu_ctx *c);
	void (*set_cb)(struct imu_ctx *c, uint32_t val);
};

struct opt3007_context {
	int (*init_cb)(struct opt3007_context *ctx);
	int (*read_cb)(int arg);
	int ready;
	uint32_t lux_base_max;
	uint8_t auto_brightness;
};

/* ------------------------------------------------------------------ storage */

/* settings backend */
struct local_store {
	int status;
	int (*write)(const char *key, const void *val, size_t len);
	int (*load)(void);
	void (*del)(const char *key);
};

struct pk7 {
	uint32_t a;
	uint16_t b;
	uint8_t c;
} __packed;

struct triple {
	uint32_t v[3];
};

/* ------------------------------------------------------------------ feature blocks */

/* GlassesState->dashboard_ts (persisted as the "dashboard_ts" setting) */
struct dashboard_ts_context {
	uint32_t ts;
	uint8_t weather_icon;
	uint8_t temperature;
	uint64_t ms_ts;                          /* uptime ms base */
	uint8_t retained_buf_a[0x25];
	uint8_t retained_buf_b[0x2a];
	uint8_t temp_unit;                       /* 0 = Celsius, else Fahrenheit */
	uint8_t time_disp_mode;                  /* 1 = 12h */
	uint8_t date_format;
	uint8_t low_power;
	uint8_t retained_a;
	uint8_t retained_b;
	uint8_t pad_63;
	uint8_t retained_c;
	uint8_t disp_mode;
	uint8_t area;                            /* custom display area */
	uint8_t sched_mode;
	uint8_t sched_num;
	uint8_t stocks_mode;
	uint8_t news_mode;
	uint8_t citywalk_mode;
	uint8_t citywalk_bg_param;
	uint16_t arrow_x;                        /* citywalk arrow offset */
	uint16_t arrow_y;
	uint8_t citywalk_state;
	uint8_t dashboard_hold;
	uint8_t dashboard_on;
	uint8_t exit_pending;
} __packed;

/* GlassesState->countdown_ts (persisted as "countdown_ts") */
struct countdown_ts_context {
	uint32_t seconds;
	uint8_t state;
	uint8_t enabled;
	uint8_t reset;
} __packed;

/* GlassesState->user_sched_info (copied from BLE request 8) */
struct user_schedule_context {
	uint8_t data[0x164];
	uint32_t ts;                             /* time stamp from the phone (0 = none) */
	uint8_t f_168;
	uint8_t f_169;
} __packed;

/* GlassesState->teleprompter */
struct teleprompter_info {
	uint8_t mode;
	uint8_t ready;
	int64_t ts;
	uint8_t action;
	uint8_t mode_flag;
	uint8_t countdown_s;
	uint8_t scroll_pct;
	uint16_t mark_pos;
	uint8_t suspend;
	uint32_t counter;
	uint16_t len;
	uint8_t text[0x200];
} __packed;

/* GlassesState->translate_src / translate_dst */
#define TRANSLATE_DATA_LEN 0x1e0

struct translate_buf {
	uint8_t translation_type;
	uint8_t original_type;
	uint8_t suspend;
	uint8_t screen_status;
	uint16_t char_pos;
	uint8_t data[TRANSLATE_DATA_LEN];
	uint16_t len;
} __packed;

/* GlassesState->nav */
struct nav_info {
	uint8_t running;
	uint8_t direction;
	uint8_t f_02[4];
	uint8_t updated;                         /* phone alive */
	uint8_t imu_action_status;
	uint16_t x;
	uint16_t y;
	uint8_t f_0c;
	char time_remaining[0x18];
	char km_remaining[0x18];
	char road_name[0x40];
	char remaining_distance[0x18];
	char speed[0x18];
	uint8_t panoramic;
	uint8_t arrived;
	uint8_t type;
	uint8_t sync_id;
	uint32_t map_size;
	char prompt[0x40];
} __packed;

/* GlassesState->bitmap_info */
struct bitmap_show_info {
	uint8_t enable;
	uint8_t sync_pending;
	uint8_t mode;
	int64_t deadline;
} __packed;

/* GlassesState->onboarding (cleared by init_onboarding_info) */
struct onboarding_ctx {
	uint8_t active;
	uint8_t in_progress;
	uint8_t action;
	uint8_t sub_step;
	uint64_t ts;                             /* k_uptime_get() of the last step change */
	uint8_t sync_arg;
	uint8_t calib_state_a;
	uint8_t calib_state_b;
	uint32_t calib_data_a;
	uint32_t calib_data_b;
	uint32_t calib_data_c;
	char step_str[4];                        /* "n/6" */
	uint8_t flags;
	uint8_t saved_brightness;
} __packed;

/* GlassesState->ts_info: page enter time stamp */
struct prompt_info {
	union {
		struct {
			uint8_t enable;
			uint8_t action_cmd;
			int64_t ts;              /* k_uptime_get() when the page was entered */
		} __packed;
		struct {
			uint32_t word_a;
			uint32_t word_b;
			uint16_t half_c;
		} __packed init;                 /* main.c clears the block as three words */
	};
} __packed;

/* GlassesState->quicknote */
struct quicknote_item {
	uint8_t valid;
	char title[0x29];
	char text[0x119];
};

struct quicknote_ctx {
	uint8_t hdr[3];
	struct quicknote_item notes[4];
};

/* ------------------------------------------------------------------ GlassesState */

/* GlassesState->esb_up_snapshot / esb_down_snapshot: block data, then the sequence byte */
#define ESB_SNAPSHOT_SEQ  0x200
#define ESB_SNAPSHOT_SIZE (ESB_SNAPSHOT_SEQ + 1)

#define HORIZ_PITCH_CFG_SIZE 2

struct GlassesState {
	uint8_t is_master;                       /* 1 master (left), 2 slave */
	uint8_t work_mode;                       /* 1 / 8: busy / 9 */
	uint8_t esb_channel;
	uint8_t esb_master_addr;
	uint8_t esb_slave_addr;
	struct k_sem ipc_sem;
	struct k_sem sem_1;
	struct k_sem sem_2;
	struct k_sem screen_on_sem;
	struct k_sem main_sem;
	struct k_sem dashboard_position_sem;
	struct k_sem sem_7;
	struct k_sem touch_sem;

	/* ESB link state */
	uint8_t esb_block;
	struct esb_hdr esb_hdr;
	uint8_t esb_arg1;
	uint8_t esb_arg2;                        /* even-AI v2 volume level */
	uint8_t esb_up_id;
	uint8_t esb_down_id;
	uint8_t esb_up_recving;
	uint8_t esb_down_recving;
	uint32_t date_ts;
	union {
		struct esb_sync_pkt sync;        /* last sync package */
		struct {
			struct esb_hdr sync_hdr;
			uint8_t dashboard_mode; /* = sync.screen_id */
			uint16_t sync_canvas_x;
			uint16_t sync_canvas_y;
			uint8_t sync_seq;
			uint8_t stay_time_left_s;
			uint8_t unknown_task_count;
			uint8_t unread_msg_num;
			uint8_t battery_min;
			struct msg_status sync_status;
			uint8_t sync_flags;
			uint8_t sync_data[4];
		} __packed;
	};
	uint8_t esb_up_seq;
	uint8_t esb_down_seq;
	uint8_t esb_dir;
	uint8_t esb_screen_id;
	union {
		struct {
			uint16_t v;
		} __packed buf_len;
		struct {
			uint8_t tx_cmd;
			uint8_t tx_arg;
		};
	};
	union screen_payload ui;
	struct esb_pkt esb_pkt;

	/* IPC (net core) */
	struct ipc_ep_cfg ipc_ep[4];
	struct ipc_service_context ipc_ctx;

	struct ble_context ble_context;
	struct k_sem imu_sem;
	uint8_t imu_busy;
	uint8_t schedule_notif_pending;
	struct spi_master_ctx spi;
	struct flash_ops flash_ops;
	struct jbd_panel_context panel;
	struct imu_ctx imu;
	struct opt3007_context opt3007_context;

	/* ambient light -> brightness */
	union {
		uint32_t lux_base[3];
		struct {
			int lux_base_low;
			int lux_base_mid;
			int lux_base_high;
		};
	};
	uint32_t lux_base_spare;
	int luminance_curve;
	int luminance_curve_a1;
	float lux_k_min;
	float lux_k_max;

	/* battery */
	union {
		uint8_t battery_info[4];
		struct {
			uint8_t battery_left;    /* own soc */
			uint8_t battery_right;
			uint8_t battery_soc_flag;
			uint8_t f_fc7;
		};
	};
	uint8_t battery_voltage;
	uint8_t battery_current;
	uint8_t battery_temp;
	int8_t battery_current_ma;
	int16_t battery_temp_x100;

	struct fw_ver master_fw_ver;
	struct fw_ver fw_ver;
	uint8_t master_mac[6];
	uint8_t slave_mac[6];
	uint8_t suspend_state;
	uint8_t panel_on;
	uint8_t screen_on;
	uint8_t imu_event;
	uint8_t display_mode;

	/* per-feature contexts */
	struct dashboard_ts_context *dashboard_ts;
	struct countdown_ts_context *countdown_ts;
	struct user_schedule_context *user_sched_info;
	struct schedule_notif *schedule_notif;
	struct teleprompter_info *teleprompter;
	struct nav_info *nav;
	struct translate_buf *translate_src;
	struct translate_buf *translate_dst;
	struct dash_data_buf *dash_buf;          /* first byte: screen state */
	struct evenai_buf *evenai_buf;           /* first byte: even-AI state */
	struct bitmap_show_info *bitmap_info;
	struct onboarding_ctx *onboarding;
	uint8_t *raster_height_cfg;
	uint8_t *horiz_pitch_cfg;                /* HORIZ_PITCH_CFG_SIZE bytes */
	struct prompt_info *ts_info;
	struct transcribe_buf *transcribe_buf;   /* ble_proto.h */
	struct quicknote_ctx *quicknote;
	struct quicknote_show_info *quicknote_show_info;
	uint8_t *esb_up_snapshot;                /* ESB_SNAPSHOT_SIZE bytes */
	uint8_t *esb_down_snapshot;
	void (*switch_to_dfu_cb)(struct k_sem *sem);
	int (*flash_read)(struct GlassesState *gs, uint32_t addr, void *buf, size_t len);
	int (*flash_write)(struct GlassesState *gs, uint32_t addr, const void *buf, size_t len);
	int (*flash_erase)(struct GlassesState *gs, uint32_t addr, size_t len);
	struct local_store store;
	int dashboard_cmd;
	int first_enter_flag;
	struct msg_status *task_status;
	uint8_t bt_state;
	uint8_t pending;                         /* refresh flags */
	uint8_t sync_ack;
	uint16_t module_mask;                    /* bit 7: BLE connected to the app */
	int64_t last_sync_ts;
	uint8_t phone_is_ios;
	union {
		bt_addr_le_t bonded_addr;
		struct pk7 bonded_addr_raw;      /* storage copies it in 3 parts */
	};
	union {
		uint16_t bond_flags;             /* main.c clears both at once */
		struct {
			uint8_t bonded;
			uint8_t force_bind;
		};
	};
	uint16_t canvas_x;
	uint16_t canvas_y;
	int32_t aw_diff;                         /* touch diff */
	int accel[3];
	uint8_t comflag;
	uint8_t ext_flash_ok;
	uint16_t flash_page_count;
	uint8_t mic_nocompress;

	/* charger */
	uint8_t aging_test_count;
	uint8_t aging_start_soc;
	uint32_t aging_start_uptime_s;
	uint32_t aging_elapsed_s;
	uint32_t aging_charge_time_s;
	uint32_t aging_discharge_time_s;
	union {
		uint16_t aging_discharge_step_s[6];
		struct triple aging_discharge_step_raw;
	};
	uint8_t aging_touch_count;
	uint32_t aging_charge_check_count;
	uint8_t aging_charge_check_result;
	uint8_t aging_no_charge_count;
	uint8_t check_mode;
	uint8_t user_data_reset_pending;
	uint8_t message_mode;
	uint8_t message_stay_time;
	uint8_t charge_limit_upper;
	uint8_t charge_limit_lower;

	uint8_t double_tap_customize;
	uint8_t double_tap_dashboard_on;
	union {
		struct u8pair {
			uint8_t a;
			uint8_t b;
		} f_1246_pair;                   /* settings record copies both */
		struct {
			uint8_t longpress_tap_customize;
			uint8_t head_up_mic_disabled;
		};
	};
} __aligned(8);

struct GlassesState *get_dashboard_state(void);

#endif /* G1_GLASSES_STATE_H_ */
