/*
 * The common fakes, one fakes_<area>.c per area; tests/unit/CMakeLists.txt picks the areas of a
 * suite. Every fake is weak, so a linked firmware definition replaces it. Only the fakes that tests
 * inspect are declared here; the others are defined with FAKE_VALUE_FUNC() in their area's file.
 */
#ifndef G1_UNIT_FAKES_H_
#define G1_UNIT_FAKES_H_

#include <zephyr/fff.h>

#include "harness.h"

struct bt_conn;
struct ble_req_hdr;

/* audio: microphone and audio pipeline; dmic_get_stop_state() is 1 on the master, as at boot */
DECLARE_FAKE_VALUE_FUNC(uint8_t, dmic_get_stop_state);
/*
 * void where production_test.c is the first file to include sensors_audio.h: include this header
 * after production_test.c there
 */
#ifndef DMIC_STREAM_STOP_VOID_PROTOTYPE
DECLARE_FAKE_VALUE_FUNC(int, dmic_stream_stop);
#endif
DECLARE_FAKE_VALUE_FUNC(int, dmic_stream_pause, int);
DECLARE_FAKE_VALUE_FUNC(uint32_t, get_dmic_num_messages);
/* -ENOMSG: no block queued */
DECLARE_FAKE_VALUE_FUNC(int, dequeue_dmic, void *);
DECLARE_FAKE_VALUE_FUNC(int, request_dmic_stream_start);
DECLARE_FAKE_VALUE_FUNC(int, delAudioStreamRecord, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, requestAudioInfoToApp, uint8_t);
extern struct k_sem mic_transm_sem;

/* bt: Bluetooth host and BLE glue */
/* never dereferenced */
#define G1_FAKE_CONN ((struct bt_conn *)&g1_fake_conn_storage)
extern uint32_t g1_fake_conn_storage;
DECLARE_FAKE_VALUE_FUNC(int, bt_unpair, uint8_t, const bt_addr_le_t *);
DECLARE_FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t);
DECLARE_FAKE_VALUE_FUNC(struct bt_conn *, get_current_conn);
DECLARE_FAKE_VOID_FUNC(ancs_delete_notification, uint32_t);

/* display: display, task, dashboard, UI and panel flush; set_date/get_date use GlassesState */
DECLARE_FAKE_VALUE_FUNC(int, display_DelayClose, int);
DECLARE_FAKE_VOID_FUNC(exit_running_task, struct GlassesState *, int);
DECLARE_FAKE_VOID_FUNC(exit_running_task_keep_status, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, update_persist_task_status, struct GlassesState *, int, int);
DECLARE_FAKE_VALUE_FUNC(bool, is_idle);
DECLARE_FAKE_VOID_FUNC(cleanCalenadrIndex);
DECLARE_FAKE_VOID_FUNC(cleanNewsIndex);
DECLARE_FAKE_VOID_FUNC(cleanNotesIndex);
DECLARE_FAKE_VOID_FUNC(cleanStocksIndex);
DECLARE_FAKE_VALUE_FUNC(uint8_t, getNotesIndex);
DECLARE_FAKE_VALUE_FUNC(uint32_t, set_date, uint32_t);
DECLARE_FAKE_VOID_FUNC(even_ai_v2_info_timeout_reset);
DECLARE_FAKE_VOID_FUNC(transcribe_info_timeout_reset);
DECLARE_FAKE_VOID_FUNC(request_not_disturb_bitmap, uint8_t);
DECLARE_FAKE_VOID_FUNC(show_not_disturb_bitmap, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, onboarding_ble_process, struct GlassesState *,
      const struct ble_req_hdr *, const uint8_t *, uint8_t *);
DECLARE_FAKE_VALUE_FUNC(int, reflash_fb_data_to_lcd, uint32_t, uint32_t, int, int, int, int);
DECLARE_FAKE_VALUE_FUNC(int, reflash_fb_data_to_lcd_ex, uint32_t, uint32_t, int, int, int, int,
      int);
DECLARE_FAKE_VOID_FUNC(jbd_clear_cache_sync, uint8_t);

/* esb: proxy thread, ESB and IPC send; send_response_data_to_msgqueue() logs the replies */
DECLARE_FAKE_VALUE_FUNC(int, SendDashBoardStartupModeInfoToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, SendDoubleTapCustomizeToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, SendLongpressTapCustomizeToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, SendSystemLanguageInfoToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, send_response_data_to_msgqueue, const void *, uint32_t);
extern struct g1_pkt_log g1_esb_response_log;
DECLARE_FAKE_VALUE_FUNC(int, send_response_data_to_ble);
/* GlassesState->ipc_ctx.send */
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_ipc_send, const void *, size_t);

/*
 * ipc: sync_to_slave() logs the data and returns the ack wait in ms (0 = at once; >= 3000 counts
 * as failed).
 */
DECLARE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);
extern struct g1_pkt_log g1_sync_to_slave_log;

/* misc: production test, threads, main.c, sensors and panel */
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_pt_level);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_pt_level_flag);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_pt_test_mode);
DECLARE_FAKE_VOID_FUNC(set_test_mode, uint8_t);
DECLARE_FAKE_VOID_FUNC(system_reboot_delayed);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_wear_state_event);
DECLARE_FAKE_VALUE_FUNC(int, trigger_imu_state_change, void *, struct GlassesState *, bool);
DECLARE_FAKE_VOID_FUNC(report_event_to_phone, int);
DECLARE_FAKE_VALUE_FUNC(uint8_t, get_dashboard_startup_mode);
DECLARE_FAKE_VALUE_FUNC(uint8_t, cur_message_show_state);
DECLARE_FAKE_VALUE_FUNC(int, display_close_screen, int);
extern uint8_t deferred_text_ack_cmd;
extern uint8_t deferred_text_ack_idx;
extern uint8_t deferred_text_ack_total;
extern atomic_t dashboard_flags;
DECLARE_FAKE_VALUE_FUNC(int, change_work_mode, int);
DECLARE_FAKE_VOID_FUNC(setEraseDFU, bool);
DECLARE_FAKE_VOID_FUNC(deinit_ncs_memory);
DECLARE_FAKE_VOID_FUNC(save_dashboard_ts_to_ncs_mem);
DECLARE_FAKE_VOID_FUNC(set_ncs_mem_initialized);
DECLARE_FAKE_VALUE_FUNC(int, set_shipmode, int);
extern char net_build_time[32];
DECLARE_FAKE_VALUE_FUNC(int, is_in_box);
extern uint32_t imu_pitch_x100;
extern atomic_t dashboard_head_up_opened;
DECLARE_FAKE_VOID_FUNC(set_brightness_to_panel_reg_in_running, uint8_t);
extern uint32_t JDB_PANEL_ID;
/* GlassesState->panel.set_brightness_cb */
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_panel_set_brightness, struct jbd_panel_context *, uint8_t);
/* what pt_comm.c, pt_data.c and production_test.c call */
DECLARE_FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
extern uint8_t can_enter_low_power;
DECLARE_FAKE_VOID_FUNC(set_ncs_mem_pt_level, uint8_t);
DECLARE_FAKE_VOID_FUNC(pt_comm_busy_set);
DECLARE_FAKE_VOID_FUNC(pt_comm_busy_clear);
DECLARE_FAKE_VALUE_FUNC(int, save_usr_settings, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(char *, get_glasses_name);

/*
 * For a test file that #includes pt_data.c and production_test.c: their statics as at boot. A
 * macro, because the statics are local to that file.
 */
#define G1_PT_STATICS_RESET()                                                                      \
  do {                                                                                       \
    pt_comm.nfc = NULL;                                                                \
    pt_comm.uart = (const struct device *)1; /* pt_comm's initializer */               \
    pt_comm.type = PT_COMM_NFC;                                                        \
    memset(pt_resp_buf, 0, sizeof(pt_resp_buf));                                       \
    memset(pt_req_buf, 0, sizeof(pt_req_buf));                                         \
    pt_test_mode = 0;                                                                  \
    pt_level_flags.raw = 0;                                                            \
    aging_mode = 0;                                                                    \
    aging_phase = 0;                                                                   \
    battery_full_flag = false;                                                         \
    battery_high_v_count = 0;                                                          \
    battery_full_count = 0;                                                            \
    battery_info_last_ms = 0;                                                          \
    charger_enable_ok = true;                                                          \
    charger_retry_last_ms = 0;                                                         \
    charger_retry_fail_count = 0;                                                      \
    memset(pt_uart_frame, 0, sizeof(pt_uart_frame));                                   \
  } while (0)

/* storage: RAM NOR flash (erased on reset, kept on reboot) and flash_settings.c */
#define G1_FAKE_FLASH_SIZE   0x800000
#define G1_FAKE_FLASH_SECTOR 0x1000
extern uint8_t g1_fake_flash_image[G1_FAKE_FLASH_SIZE];
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_flash_read, struct GlassesState *, uint32_t, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_flash_write, struct GlassesState *, uint32_t, const void *,
      size_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_flash_erase, struct GlassesState *, uint32_t, size_t);
DECLARE_FAKE_VALUE_FUNC(int, flash_settings_read, uint32_t, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, flash_settings_write_and_verify, uint32_t, const void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, write_whitelist_to_flash, uint32_t, const uint8_t *, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, write_font, uint32_t, const void *, size_t);
/* for suites without file_storage.c */
DECLARE_FAKE_VOID_FUNC(storage_commit_request, int, uint8_t);
extern struct k_sem flash_ops_sem;
extern uint8_t sys_settings_dirty;
extern uint32_t LAST_FILE_CRC;

#endif /* G1_UNIT_FAKES_H_ */
