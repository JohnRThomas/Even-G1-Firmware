/* Slots 0..9 are shown messages, 10..19 the push queue. */
#include "notif_test.h"
#include "fakes_display.h"

#define T0 1762900000u

static struct notif_msg msg;

/* ---- push_message ---- */

ZTEST(notifications, test_push_message_appends_to_the_queue_from_slot_10)
{
	make_msg(&msg, 1, "com.discord", "first");
	zassert_equal(push_message(&msg), PUSH_FIRST + 1);
	make_msg(&msg, 2, "com.discord", "second");
	zassert_equal(push_message(&msg), PUSH_FIRST + 2);

	zassert_equal(g_arrMsgContent[PUSH_FIRST].uid, 1);
	zassert_equal(g_arrMsgContent[PUSH_FIRST].pos, PUSH_FIRST);
	zassert_equal(g_arrMsgContent[PUSH_FIRST + 1].uid, 2);
	zassert_equal(g_arrMsgContent[PUSH_FIRST + 1].pos, PUSH_FIRST + 1);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 0);
}

ZTEST(notifications, test_push_message_refuses_a_held_new_uid_and_an_unknown_update)
{
	make_msg(&msg, 7, "com.discord", "first");
	zassert_equal(push_message(&msg), PUSH_FIRST + 1);

	make_msg(&msg, 7, "com.discord", "again");
	zassert_equal(push_message(&msg), 0);
	make_msg(&msg, 8, "com.discord", "update");
	msg.action = 1;
	zassert_equal(push_message(&msg), 0);

	zassert_equal(g_msg_num_push, PUSH_FIRST + 1);
	assert_str(g_arrMsgContent[PUSH_FIRST].title, "first");
}

ZTEST(notifications, test_find_push_message_by_uid_looks_at_the_free_queue_entries_only)
{
	put_slot(0, 5, "com.discord", T0, 0, 0);
	put_slot(PUSH_FIRST, 6, "com.discord", T0, 0, 0);
	put_slot(PUSH_FIRST + 1, 7, "com.discord", T0, 0, 0);
	g_arrMsgContent[PUSH_FIRST + 1].busy = 1;

	zassert_is_null(find_push_message_by_uid(5), "shown message found");
	zassert_equal_ptr(find_push_message_by_uid(6), &g_arrMsgContent[PUSH_FIRST]);
	zassert_is_null(find_push_message_by_uid(7), "busy entry found");
	zassert_is_null(find_push_message_by_uid(99));
}

/* ---- confirm_message ---- */

ZTEST(notifications, test_confirm_message_moves_the_queue_head_to_the_first_free_shown_slot)
{
	g1_clock_set_date(T0);
	put_slot(0, 1, "com.discord", T0 - 60, 0, 0);
	put_slot(PUSH_FIRST, 11, "com.discord", T0 - 5, 0, 0);
	put_slot(PUSH_FIRST + 1, 12, "com.discord", T0 - 1, 0, 0);
	g_msg_num_push = PUSH_FIRST + 2;
	set_msg_sync_state(PUSH_FIRST, 4);

	confirm_message(11);

	zassert_equal(g_arrMsgContent[0].uid, 1);
	zassert_equal(g_arrMsgContent[1].uid, 11);
	zassert_equal(g_arrMsgContent[1].pos, 1);
	zassert_equal(g_arrMsgContent[1].recv_time, T0 - 5);
	zassert_equal(g_arrMsgContent[PUSH_FIRST].uid, 12);
	zassert_equal(g_arrMsgContent[PUSH_FIRST].pos, PUSH_FIRST);
	zassert_equal(slots_used(PUSH_FIRST + 1, MSG_TOTAL_NUM), 0);
	zassert_equal(g_msg_num_push, PUSH_FIRST + 1);
	zassert_equal(g1_state()->unread_msg_num, 2);
}

ZTEST(notifications, test_confirm_message_ignores_another_uid_and_an_unsynced_head)
{
	put_slot(PUSH_FIRST, 11, "com.discord", T0, 0, 0);
	g_msg_num_push = PUSH_FIRST + 1;

	set_msg_sync_state(PUSH_FIRST, 4);
	confirm_message(12);
	zassert_equal(g_arrMsgContent[PUSH_FIRST].uid, 11, "head confirmed by another uid");

	set_msg_sync_state(PUSH_FIRST, 3);
	confirm_message(11);
	zassert_equal(g_arrMsgContent[PUSH_FIRST].uid, 11, "unsynced head confirmed");
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 0);
}

ZTEST(notifications, test_confirm_message_with_10_shown_drops_the_oldest)
{
	for (int i = 0; i < MSG_NORMAL_NUM; i++) {
		put_slot(i, i + 1, "com.discord", T0, 0, 0);
	}
	put_slot(PUSH_FIRST, 11, "com.discord", T0, 0, 0);
	g_msg_num_push = PUSH_FIRST + 1;
	set_msg_sync_state(PUSH_FIRST, 4);

	confirm_message(11);

	for (int i = 0; i < MSG_NORMAL_NUM; i++) {
		zassert_equal(g_arrMsgContent[i].uid, i + 2, "slot %d uid %u", i,
			      g_arrMsgContent[i].uid);
		zassert_equal(g_arrMsgContent[i].pos, i);
	}
	zassert_equal(g1_state()->unread_msg_num, MSG_NORMAL_NUM);
}

ZTEST(notifications, test_confirm_message_action_1_updates_the_shown_message)
{
	put_slot(0, 1, "com.discord", T0, 0, 0);
	put_slot(1, 2, "com.discord", T0, 0, 0);
	put_slot(PUSH_FIRST, 2, "com.discord", T0, 0, 0);
	strcpy(g_arrMsgContent[PUSH_FIRST].title, "edited");
	g_arrMsgContent[PUSH_FIRST].action = 1;
	g_msg_num_push = PUSH_FIRST + 1;
	set_msg_sync_state(PUSH_FIRST, 4);

	confirm_message(2);

	zassert_equal(g_arrMsgContent[1].uid, 2);
	assert_str(g_arrMsgContent[1].title, "edited");
	zassert_equal(g_arrMsgContent[0].uid, 1);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 2);
}

ZTEST(notifications, test_confirm_message_on_the_master_drops_a_uid_already_shown)
{
	put_slot(0, 5, "com.discord", T0, 0, 0);
	put_slot(PUSH_FIRST, 5, "com.discord", T0, 0, 0);
	g_msg_num_push = PUSH_FIRST + 1;
	set_msg_sync_state(PUSH_FIRST, 4);

	confirm_message(5);

	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 1);
	zassert_equal(slots_used(PUSH_FIRST, MSG_TOTAL_NUM), 0);
}

/* DEFECT: the duplicate drop clears slot 10 without moving the queue up, leaving an empty head. */
ZTEST_EXPECT_FAIL(notifications, test_confirm_message_after_dropping_a_duplicate_the_next_message_is_the_head);
ZTEST(notifications, test_confirm_message_after_dropping_a_duplicate_the_next_message_is_the_head)
{
	put_slot(0, 5, "com.discord", T0, 0, 0);
	put_slot(PUSH_FIRST, 5, "com.discord", T0, 0, 0);
	g_msg_num_push = PUSH_FIRST + 1;
	set_msg_sync_state(PUSH_FIRST, 4);
	confirm_message(5);

	make_msg(&msg, 6, "com.discord", "next");
	push_message(&msg);

	zassert_not_null(get_push_message(), "queue head is empty");
	zassert_equal(get_push_message()->uid, 6);
}

/* ---- count_unread_messages ---- */

ZTEST(notifications, test_count_unread_messages_counts_unread_up_to_the_first_empty_slot)
{
	put_slot(0, 1, "com.discord", T0, 0, 0);
	put_slot(1, 2, "com.discord", T0, T0, 1);
	put_slot(2, 3, "com.discord", T0, 0, 0);
	put_slot(4, 5, "com.discord", T0, 0, 0);

	zassert_equal(count_unread_messages(), 2);
	zassert_equal(count_messages(), 3);
}

ZTEST(notifications, test_count_unread_messages_is_0_without_messages)
{
	zassert_equal(count_unread_messages(), 0);
	zassert_equal(count_messages(), 0);
}

/* ---- clear_timeout_message ---- */

ZTEST(notifications, test_clear_timeout_read_message_stays_message_stay_time_seconds)
{
	static const uint8_t stay_times[] = {6, 15};

	for (size_t i = 0; i < ARRAY_SIZE(stay_times); i++) {
		const unsigned int stay = stay_times[i];

		g1_unit_reset();
		g1_state()->message_stay_time = stay;
		put_slot(0, 1, "com.discord", T0 - 10, T0, 1);

		g1_clock_set_date(T0 + stay);
		clear_timeout_message(0);
		zassert_equal(g_arrMsgContent[0].uid, 1, "stay time %u: dropped after %u s", stay,
			      stay);

		g1_clock_set_date(T0 + stay + 1);
		clear_timeout_message(0);
		zassert_equal(slots_used(0, MSG_NORMAL_NUM), 0, "stay time %u: kept after %u s",
			      stay, stay + 1);
	}
}

ZTEST(notifications, test_clear_timeout_unread_message_is_kept)
{
	g1_state()->message_stay_time = 6;
	put_slot(0, 1, "com.discord", T0, 0, 0);

	g1_clock_set_date(T0 + 86400);
	clear_timeout_message(0);

	zassert_equal(g_arrMsgContent[0].uid, 1);
	zassert_equal(g1_state()->unread_msg_num, 1);
}

ZTEST(notifications, test_clear_timeout_incoming_call_goes_5_s_after_it_was_shown)
{
	g1_state()->message_stay_time = 60;
	put_slot(0, 1, "com.android.phone_incall", T0, T0, 0);

	g1_clock_set_date(T0 + 5);
	clear_timeout_message(0);
	zassert_equal(g_arrMsgContent[0].uid, 1, "call dropped after 5 s");

	g1_clock_set_date(T0 + 6);
	clear_timeout_message(0);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 0, "call kept after 6 s");
}

ZTEST(notifications, test_clear_timeout_force_drops_a_read_message_at_once)
{
	g1_state()->message_stay_time = 60;
	g1_clock_set_date(T0);
	put_slot(0, 1, "com.discord", T0 - 3, T0 - 1, 1);
	put_slot(1, 2, "com.discord", T0 - 2, 0, 0);

	clear_timeout_message(1);

	zassert_equal(g_arrMsgContent[0].uid, 2);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 1);
	zassert_equal(g1_state()->unread_msg_num, 1);
}

/* DEFECT: a message moved into a dropped slot is not checked again. */
ZTEST_EXPECT_FAIL(notifications, test_clear_timeout_force_drops_every_read_message_at_once);
ZTEST(notifications, test_clear_timeout_force_drops_every_read_message_at_once)
{
	g1_state()->message_stay_time = 60;
	g1_clock_set_date(T0);
	put_slot(0, 1, "com.discord", T0 - 3, T0 - 1, 1);
	put_slot(1, 2, "com.discord", T0 - 2, 0, 0);
	put_slot(2, 3, "com.discord", T0 - 1, T0, 1);

	clear_timeout_message(1);

	zassert_equal(g_arrMsgContent[0].uid, 2);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 1);
	zassert_equal(g1_state()->unread_msg_num, 1);
}

ZTEST(notifications, test_clear_timeout_moves_the_remaining_messages_up_in_order)
{
	g1_state()->message_stay_time = 6;
	put_slot(0, 1, "com.discord", T0 - 30, 0, 0);
	put_slot(1, 2, "com.discord", T0 - 20, T0 - 10, 1);
	put_slot(2, 3, "com.discord", T0 - 5, 0, 0);
	put_slot(3, 4, "com.discord", T0 - 4, 0, 0);
	g1_clock_set_date(T0);

	clear_timeout_message(0);

	zassert_equal(g_arrMsgContent[0].uid, 1);
	zassert_equal(g_arrMsgContent[1].uid, 3);
	zassert_equal(g_arrMsgContent[2].uid, 4);
	zassert_equal(slots_used(0, MSG_NORMAL_NUM), 3);
	zassert_equal(g1_state()->unread_msg_num, 3);
}

/* ---- mark_cur_message_read ---- */

ZTEST(notifications, test_mark_cur_message_read_marks_it_and_starts_its_stay_time)
{
	put_slot(0, 1, "com.discord", T0 - 30, 0, 0);
	g1_state()->unread_msg_num = 1;
	g_curShowMsgIdx = 0;
	g1_clock_set_date(T0);

	mark_cur_message_read();

	zassert_equal(g_arrMsgContent[0].read, 1);
	zassert_equal(g_arrMsgContent[0].start_show_time, T0);
	zassert_equal(g1_state()->unread_msg_num, 0);
}

ZTEST(notifications, test_mark_cur_message_read_leaves_a_read_message_alone)
{
	put_slot(0, 1, "com.discord", T0 - 30, T0 - 10, 1);
	g_curShowMsgIdx = 0;
	g1_clock_set_date(T0);

	mark_cur_message_read();

	zassert_equal(g_arrMsgContent[0].start_show_time, T0 - 10);
}

/* ---- parse_message_type ---- */

static uint8_t type_of(const char *app)
{
	put_slot(0, 1, app, T0, 0, 0);
	return parse_message_type(0);
}

ZTEST(notifications, test_parse_message_type_android_packages)
{
	zassert_equal(type_of("com.android.phone_missed"), 1);
	zassert_equal(type_of("com.android.phone_incall"), 2);
	zassert_equal(type_of("com.android.even_sms"), 0);
	zassert_equal(type_of("com.google.android.gm"), 3);
	zassert_equal(type_of("com.microsoft.office.outlook"), 3);
	/* longer than app_identifier: compared on its first 31 bytes */
	zassert_equal(type_of("com.yahoo.mobile.client.android.mail"), 3);
	zassert_equal(type_of("com.google.android.calendar"), 4);
	zassert_equal(type_of("com.android.even_calendar"), 4);
	zassert_equal(type_of("com.discord"), 5);
	zassert_equal(type_of("com.google.Gmail"), 5, "iOS mail on an Android phone");
}

ZTEST(notifications, test_parse_message_type_ios_packages)
{
	g1_state()->phone_is_ios = 1;

	zassert_equal(type_of("com.apple.mobilephone_missed"), 1);
	zassert_equal(type_of("com.apple.mobilephone"), 2);
	zassert_equal(type_of("com.apple.MobileSMS"), 0);
	zassert_equal(type_of("com.apple.mail"), 3);
	zassert_equal(type_of("com.google.Gmail"), 3);
	zassert_equal(type_of("com.apple.mobilecal"), 4);
	zassert_equal(type_of("com.google.calendar"), 4);
	zassert_equal(type_of("com.google.android.gm"), 5, "Android mail on an iPhone");
	zassert_equal(type_of("net.whatsapp.WhatsApp"), 5);
}
