/* 0x4B: 4B <pack id> <count> <index> <JSON part>; reply 4B <CB more | C9 last | CA error> 00. */
#include "notif_test.h"
#include "fakes_bt.h"
#include "fakes_display.h"

ZTEST_SUITE(notifications, NULL, NULL, NULL, NULL, NULL);

static uint8_t pkt[256];

#define LOG_UID 413

static void assert_log_message(const struct notif_msg *m)
{
	zassert_not_null(m);
	zassert_equal(m->uid, LOG_UID);
	zassert_equal(m->action, 0);
	assert_str(m->app_identifier, "nodomain.freeyourgadget.gadgetb");
	assert_str(m->title, "Even G1_87");
	assert_str(m->subtitle, "");
	assert_str(m->message, "Connected: Battery 98%, 100%, 10%");
	assert_str(m->date, "2025-11-11 14:25:59");
	assert_str(m->display_name, "Gadgetbridge");
}

static void send_log_notification(void)
{
	g1_send_vector("0x4b_notification_chunk1");
	g1_send_vector("0x4b_notification_chunk2");
}

/* ---- 0x4B ---- */

ZTEST(notifications, test_0x4b_two_chunks_reply_cb_then_c9)
{
	set_whitelist(log_whitelist_json);

	g1_send_vector("0x4b_notification_chunk1");
	expect_frame_padded("4b cb 00", 20);
	expect_no_frame();
	g1_send_vector("0x4b_notification_chunk2");
	expect_frame_padded("4b c9 00", 20);
	expect_no_frame();
}

ZTEST(notifications, test_0x4b_two_chunks_queue_the_message_with_its_fields)
{
	struct notif_msg *m;

	set_whitelist(log_whitelist_json);
	g1_clock_set_date(1762900000);
	send_log_notification();

	m = find_push_message_by_uid(LOG_UID);
	assert_log_message(m);
	zassert_equal_ptr(m, &g_arrMsgContent[PUSH_FIRST]);
	zassert_equal(m->recv_time, 1762900000);
	zassert_equal(m->start_show_time, 0);
	zassert_equal(m->pos, PUSH_FIRST);
	zassert_equal(m->read, 0);
	zassert_equal(m->sync_state, 0);
	zassert_equal(m->busy, 0);
	zassert_equal(g_msg_num_push, PUSH_FIRST + 1);
	zassert_equal(slots_used(PUSH_FIRST + 1, MSG_TOTAL_NUM), 0);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 0);
	zassert_equal_ptr(get_push_message(), m);
}

ZTEST(notifications, test_0x4b_message_counts_as_unread_once_confirmed)
{
	set_whitelist(log_whitelist_json);
	send_log_notification();
	g1_frames_clear();

	zassert_equal(count_unread_messages(), 0);
	zassert_equal(g1_state()->unread_msg_num, 0);

	set_msg_sync_state(PUSH_FIRST, 4);
	confirm_message(LOG_UID);

	zassert_equal(count_unread_messages(), 1);
	zassert_equal(g1_state()->unread_msg_num, 1);
	assert_log_message(&g_arrMsgContent[0]);
	zassert_is_null(find_push_message_by_uid(LOG_UID));
	zassert_equal(g_msg_num_push, PUSH_FIRST);
}

/* any chunk size is accepted */
ZTEST(notifications, test_0x4b_three_chunks_reply_cb_cb_c9_and_queue_the_message)
{
	static const char json[] =
		"{\"ncs_notification\":{\"msg_id\":16,\"action\":0,"
		"\"app_identifier\":\"nodomain.freeyourgadget.gadgetb\","
		"\"title\":\"Test notification\",\"subtitle\":\"\","
		"\"message\":\"This is a test notification from Gadgetbridge\","
		"\"time_s\":1749606217,\"date\":\"2025-06-10 18:43:37\","
		"\"display_name\":\"Gadgetbridge\"}}";
	struct notif_msg *m;

	set_whitelist(log_whitelist_json);
	zassert_equal(strlen(json), 275);
	zassert_equal(send_ncs(json, 100), 3);

	expect_4b_reply(0xcb);
	expect_4b_reply(0xcb);
	expect_4b_reply(0xc9);
	expect_no_frame();
	m = find_push_message_by_uid(16);
	zassert_not_null(m);
	assert_str(m->title, "Test notification");
	assert_str(m->message, "This is a test notification from Gadgetbridge");
	assert_str(m->date, "2025-06-10 18:43:37");
}

/* DEFECT: only chunk 0's header is kept; the mixed message is queued and acked C9. */
ZTEST_EXPECT_FAIL(notifications, test_0x4b_chunk_count_changing_mid_message_is_refused);
ZTEST(notifications, test_0x4b_chunk_count_changing_mid_message_is_refused)
{
	const char *json = ncs_json(77, 0, "com.discord", "Ping", "hello", "Discord");
	size_t len = strlen(json);

	set_whitelist(log_whitelist_json);
	pkt[0] = 0x4b;
	pkt[1] = 0x00;
	pkt[2] = 3;
	pkt[3] = 0;
	memcpy(&pkt[4], json, 100);
	g1_send(pkt, 104);
	expect_4b_reply(0xcb);

	pkt[2] = 2;
	pkt[3] = 1;
	memcpy(&pkt[4], json + 100, len - 100);
	g1_send(pkt, 4 + len - 100);

	zassert_is_null(find_push_message_by_uid(77), "message of mixed chunks queued");
	expect_4b_reply(0xca);
}

/* DEFECT: a failed parse still queues the buffer; its empty app id passes the whitelist. */
ZTEST_EXPECT_FAIL(notifications, test_0x4b_message_that_is_not_json_queues_nothing);
ZTEST(notifications, test_0x4b_message_that_is_not_json_queues_nothing)
{
	send_ncs("{\"ncs_notification\":{\"msg_id\":5,\"action\":0,", NCS_CHUNK_JSON);
	g1_frames_clear();

	zassert_equal(g_msg_num_push, PUSH_FIRST, "%d entries queued",
		      g_msg_num_push - PUSH_FIRST);
}

/* DEFECT: the reply is C9 whatever the parse gave. */
ZTEST_EXPECT_FAIL(notifications, test_0x4b_message_that_is_not_json_is_answered_ca);
ZTEST(notifications, test_0x4b_message_that_is_not_json_is_answered_ca)
{
	send_ncs("{\"ncs_notification\":{\"msg_id\":5,\"action\":0,", NCS_CHUNK_JSON);

	expect_4b_reply(0xca);
}

ZTEST(notifications, test_0x4b_duplicate_msg_id_is_queued_once)
{
	set_whitelist(log_whitelist_json);
	send_log_notification();
	send_log_notification();

	expect_4b_reply(0xcb);
	expect_4b_reply(0xc9);
	expect_4b_reply(0xcb);
	expect_4b_reply(0xc9);
	zassert_equal(g_msg_num_push, PUSH_FIRST + 1);
	zassert_equal(slots_used(PUSH_FIRST, MSG_TOTAL_NUM), 1);
}

ZTEST(notifications, test_0x4b_msg_id_already_shown_is_not_queued_again)
{
	set_whitelist(log_whitelist_json);
	put_slot(0, LOG_UID, "nodomain.freeyourgadget.gadgetb", G1_RESET_DATE, 0, 0);

	send_log_notification();
	g1_frames_clear();

	zassert_equal(g_msg_num_push, PUSH_FIRST);
	zassert_equal(slots_used(PUSH_FIRST, MSG_TOTAL_NUM), 0);
}

/* The F6 frames carry {"whitelist_app_add": ...} in 17-byte parts. */
ZTEST(notifications, test_0x4b_app_not_on_the_list_is_announced_with_f6_and_not_queued)
{
	set_whitelist("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		      "\"ios_mail_enable\":true,\"app\":{\"list\":[{\"id\":"
		      "\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"}],"
		      "\"enable\":true}}");

	send_ncs(ncs_json(424, 0, "com.google.android.gm", "New mail", "", "Gmail"),
		 NCS_CHUNK_JSON);

	expect_4b_reply(0xcb);
	expect_frame("f6 06 00 7b 22 77 68 69 74 65 6c 69 73 74 5f 61 70 70 5f 61");
	expect_frame("f6 06 01 64 64 22 3a 20 7b 22 61 70 70 5f 69 64 65 6e 74 69");
	expect_frame("f6 06 02 66 69 65 72 22 3a 20 20 22 63 6f 6d 2e 67 6f 6f 67");
	expect_frame("f6 06 03 6c 65 2e 61 6e 64 72 6f 69 64 2e 67 6d 22 2c 22 64");
	expect_frame("f6 06 04 69 73 70 6c 61 79 5f 6e 61 6d 65 22 3a 20 22 47 6d");
	expect_frame("f6 06 05 61 69 6c 22 7d 7d");
	expect_4b_reply(0xc9);
	expect_no_frame();
	zassert_equal(g_msg_num_push, PUSH_FIRST);
	zassert_is_null(find_push_message_by_uid(424));
}

ZTEST(notifications, test_0x4b_incoming_call_follows_call_enable)
{
	set_whitelist("{\"calendar_enable\":true,\"call_enable\":false,\"msg_enable\":true,"
		      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}");
	send_ncs_expect_ok(ncs_json(40, 0, "com.android.phone_incall", "Alice", "", "Phone"));
	zassert_is_null(find_push_message_by_uid(40), "call queued with call_enable false");

	set_whitelist("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}");
	g1_clock_set_date(1762900100);
	send_ncs_expect_ok(ncs_json(41, 0, "com.android.phone_incall", "Alice", "", "Phone"));
	zassert_not_null(find_push_message_by_uid(41));
	zassert_equal(find_push_message_by_uid(41)->start_show_time, 1762900100);
}

ZTEST(notifications, test_0x4b_sms_follows_msg_enable)
{
	set_whitelist("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":false,"
		      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}");
	send_ncs_expect_ok(ncs_json(50, 0, "com.android.even_sms", "Bob", "see you", "SMS"));
	zassert_is_null(find_push_message_by_uid(50), "SMS queued with msg_enable false");

	set_whitelist("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}");
	send_ncs_expect_ok(ncs_json(51, 0, "com.android.even_sms", "Bob", "see you", "SMS"));
	zassert_not_null(find_push_message_by_uid(51));
}

ZTEST(notifications, test_0x4b_21st_message_replaces_the_oldest_queued_one)
{
	set_whitelist(log_whitelist_json);
	for (int i = 0; i < MSG_TOTAL_NUM; i++) {
		put_slot(i, i + 1, "com.discord", G1_RESET_DATE, 0, 0);
	}
	g_msg_num_push = MSG_TOTAL_NUM;

	send_ncs_expect_ok(ncs_json(21, 0, "com.discord", "Ping", "21st", "Discord"));

	for (int i = 0; i < MSG_NORMAL_NUM; i++) {
		zassert_equal(g_arrMsgContent[i].uid, i + 1, "shown slot %d", i);
	}
	for (int i = PUSH_FIRST; i < MSG_TOTAL_NUM - 1; i++) {
		zassert_equal(g_arrMsgContent[i].uid, i + 2, "queue slot %d uid %u", i,
			      g_arrMsgContent[i].uid);
		zassert_equal(g_arrMsgContent[i].pos, i);
	}
	zassert_equal(g_arrMsgContent[MSG_TOTAL_NUM - 1].uid, 21);
	assert_str(g_arrMsgContent[MSG_TOTAL_NUM - 1].message, "21st");
	zassert_equal(g_msg_num_push, MSG_TOTAL_NUM);
}

/* ---- 0x36 ---- */

ZTEST(notifications, test_0x36_count_is_the_number_of_unread_shown_messages)
{
	set_whitelist(log_whitelist_json);
	send_ncs_expect_ok(ncs_json(60, 0, "com.discord", "One", "1", "Discord"));
	set_msg_sync_state(PUSH_FIRST, 4);
	confirm_message(60);
	send_ncs_expect_ok(ncs_json(61, 0, "com.discord", "Two", "2", "Discord"));
	set_msg_sync_state(PUSH_FIRST, 4);
	confirm_message(61);

	g1_send_vector("0x36_notification_count_get");
	expect_frame_padded("36 c9 02", 20);

	mark_cur_message_read();
	g1_send_vector("0x36_notification_count_get");
	expect_frame_padded("36 c9 01", 20);
}

/* ---- 0x4C ---- */

/* id 424, big-endian */
ZTEST(notifications, test_0x4c_clear_passes_the_big_endian_id_to_ancs_and_replies_c9)
{
	g1_send_vector("0x4c_clear");

	expect_frame_padded("4c c9", 20);
	expect_no_frame();
	zassert_equal(ancs_delete_notification_fake.call_count, 1);
	zassert_equal(ancs_delete_notification_fake.arg0_val, 424);
}

ZTEST(notifications, test_0x4c_unknown_id_is_passed_on_and_changes_no_message)
{
	put_slot(0, LOG_UID, "com.discord", G1_RESET_DATE, 0, 0);
	put_slot(PUSH_FIRST, 500, "com.discord", G1_RESET_DATE, 0, 0);
	g_msg_num_push = PUSH_FIRST + 1;

	g1_send_hex("4c 00 00 00 1b");

	expect_frame_padded("4c c9", 20);
	zassert_equal(ancs_delete_notification_fake.arg0_val, 27);
	zassert_equal(g_arrMsgContent[0].uid, LOG_UID);
	zassert_equal(g_arrMsgContent[PUSH_FIRST].uid, 500);
	zassert_equal(g_msg_num_push, PUSH_FIRST + 1);
}

/* ---- ANCS entry ---- */

/* Source 2: the ANCS module hands over a finished struct. */
ZTEST(notifications, test_ancs_source_2_queues_the_struct_and_sends_nothing)
{
	uint8_t req[4] = {0x4b, 2, 0, 0};
	struct notif_msg msg;
	struct notif_msg *m;

	set_whitelist(log_whitelist_json);
	make_msg(&msg, 900, "com.apple.MobileSMS", "Carol");
	strcpy(msg.message, "on my way");

	post_notification_cmd_process(g1_ble(), req, (uint8_t *)&msg);

	expect_no_frame();
	m = find_push_message_by_uid(900);
	zassert_not_null(m);
	assert_str(m->app_identifier, "com.apple.MobileSMS");
	assert_str(m->title, "Carol");
	assert_str(m->message, "on my way");
	zassert_equal(m->recv_time, msg.recv_time);
}

/* Source 4: a calendar alert */
ZTEST(notifications, test_ancs_source_4_sets_the_schedule_notification_and_sends_nothing)
{
	uint8_t req[4] = {0x4b, 4, 0, 0};
	struct notif_msg msg;
	struct GlassesState *gs = g1_state();

	make_msg(&msg, 901, "com.apple.mobilecal", "Team meeting");
	strcpy(msg.message, "Room 4, 10:00");
	gs->schedule_notif->ts = 1234;

	post_notification_cmd_process(g1_ble(), req, (uint8_t *)&msg);

	expect_no_frame();
	assert_str(gs->schedule_notif->title, "Team meeting");
	assert_str(gs->schedule_notif->desc, "Room 4, 10:00");
	zassert_equal(gs->schedule_notif->ts, 0);
	zassert_equal(gs->schedule_notif_pending, 1);
	zassert_equal(g_msg_num_push, PUSH_FIRST);
}

ZTEST(notifications, test_ancs_removal_takes_the_message_off_the_lens_once_confirmed)
{
	uint8_t req[4] = {0x4b, 2, 0, 0};
	struct notif_msg msg;

	put_slot(0, 422, "com.discord", G1_RESET_DATE, 0, 0);
	put_slot(1, 424, "com.google.android.gm", G1_RESET_DATE, 0, 0);
	put_slot(2, 425, "com.discord", G1_RESET_DATE, 0, 0);
	make_msg(&msg, 424, "com.google.android.gm", "");
	msg.action = 2;

	post_notification_cmd_process(g1_ble(), req, (uint8_t *)&msg);
	expect_no_frame();
	zassert_equal(g_arrMsgContent[PUSH_FIRST].uid, 424);
	set_msg_sync_state(PUSH_FIRST, 4);
	confirm_message(424);

	zassert_equal(g_arrMsgContent[0].uid, 422);
	zassert_equal(g_arrMsgContent[1].uid, 425);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 2);
	zassert_equal(g_msg_num_push, PUSH_FIRST);
	zassert_equal(g1_state()->unread_msg_num, 2);
}
