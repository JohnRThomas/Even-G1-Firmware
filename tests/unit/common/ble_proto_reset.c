/*
 * Builds src/ble/ble_proto.c, unmodified, for the unit suites (g1_unit_firmware(ble_proto)),
 * together with ble_proto_test_reset(), which needs its static variables.
 */
#include "ble/ble_proto.c"

/*
 * Puts every file-scope variable of ble_proto.c back to its boot value and sets up the kernel
 * objects that start_ble_work_thread() and ble_work_thread() initialise, without starting the
 * thread. The harness calls it before each test.
 */
void ble_proto_test_reset(void)
{
	static bool kernel_objects_ready;

	if (kernel_objects_ready) {
		k_timer_stop(&DashboardLockTimer);
		k_msgq_purge(&debug_msgq);
	} else {
		debug_init();
		kernel_objects_ready = true;
	}
	k_mutex_init(&nav_map_mutex);
	k_timer_init(&DashboardLockTimer, DashboardLockTimerExpiry_callback, NULL);

	memset(nav_overview_map_buf, 0, sizeof(nav_overview_map_buf));
	memset(panoramic_map_buf, 0, sizeof(panoramic_map_buf));
	sched_cnt = 0;
	sched_total = 0;
	sched_len = 0;
	memset(stock_items, 0, sizeof(stock_items));
	stock_cnt = 0;
	stock_total = 0;
	stock_len = 0;
	news_cnt = 0;
	news_total = 0;
	news_len = 0;
	citywalk_len = 0;
	multi_packet_len = 0;
	multi_packet_cnt = 0;
	multi_packet_total = 0;
	tp_len = 0;
	pano_len = 0;
	pano_cnt = 0;
	pano_total = 0;
	qn_len = 0;
	qn_cnt = 0;
	qn_total = 0;
	nav_last_x = 0;
	nav_last_y = 0;
	pack_offset = 0;
	msg_pack_offset = 0;
	msg_pack_id = 0;

	atomic_set(&dashboard_lock_requested, 0);
	atomic_set(&dashboard_lock_reported, 0);
	nav_app_sync_count = 0;
	tp_app_sync_count = 0;
	dmic_send_count = 0;
	dmic_pkt_count = 0;
	send_fail_count = 0;
	dashboard_send_fail_count = 0;
	BLE_DEBUG = 0;
	debug_pending = 0;

	citywalk_reload = 1;
	BIT_MAP_CHUNK_ID = 0xff;
	memset(schedule_items, 0, sizeof(schedule_items));
	memset(sched_buf, 0, sizeof(sched_buf));
	sched_retry = 0;
	memset(stock_buf, 0, sizeof(stock_buf));
	memset(news_items, 0, sizeof(news_items));
	memset(news_buf, 0, sizeof(news_buf));
	citywalk_last_disp_mode = 0;
	citywalk_bg_ready = 0;
	citywalk_canvas_valid = 0;
	memset(&tp_buf, 0, sizeof(tp_buf));
	tp_scroll_pending = 0;
	memset(&qn_title_buf, 0, sizeof(qn_title_buf));
	memset(qn_text_buf, 0, sizeof(qn_text_buf));
	qn_num = 0;
	nav_overview_map_ready = 0;
	nav_panoramic_map_ready = 0;
	nav_text_updated = 0;
	nav_overview_map_updated = 0;
	nav_panoramic_map_updated = 0;
	nav_pos_updated = 0;
	dmic_seq = 0;
	pack_cmd = 0;
	msg_pack_cmd = 0;
	memset(debug_buf, 0, sizeof(debug_buf));
	memset(text_packet_buf, 0, sizeof(text_packet_buf));
}
