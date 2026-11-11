#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "notify/notify.h"

ZTEST_SUITE(json, NULL, NULL, NULL, NULL, NULL);

static struct notif_msg msg;
static char json_buf[2048];

/* 0xaa shows cleared vs untouched */
static void parse(const char *json)
{
	zassert_true(strlen(json) < sizeof(json_buf));
	strcpy(json_buf, json);
	memset(&msg, 0xaa, sizeof(msg));
	parse_ncs_notification(json_buf, &msg);
}

static void assert_untouched(void)
{
	static uint8_t pattern[sizeof(struct notif_msg)];

	memset(pattern, 0xaa, sizeof(pattern));
	zassert_mem_equal(&msg, pattern, sizeof(msg));
}

static void assert_zero(const void *field, size_t len, const char *what)
{
	const uint8_t *p = field;

	for (size_t i = 0; i < len; i++) {
		zassert_equal(p[i], 0, "%s byte %u is 0x%02x", what, (unsigned int)i, p[i]);
	}
}

#define ASSERT_ZERO_FIELD(f) assert_zero(&msg.f, sizeof(msg.f), #f)

/* no zassert_str_equal() in Zephyr 3.4 */
#define assert_str(got, want)                                                                      \
	zassert_true(strcmp((got), (want)) == 0, "%s is \"%s\", expected \"%s\"", #got, (got),     \
		     (want))

ZTEST(json, test_ncs_official_app_payload_fills_every_field)
{
	g1_clock_set_date(1762899959);

	parse("{\"ncs_notification\":{\"msg_id\":413,\"action\":0,"
	      "\"app_identifier\":\"nodomain.freeyourgadget.gadgetb\",\"title\":\"Even G1_87\","
	      "\"subtitle\":\"\",\"message\":\"Connected: Battery 98%, 100%, 10%\","
	      "\"time_s\":1762899959,\"date\":\"2025-11-11 14:25:59\","
	      "\"display_name\":\"Gadgetbridge\"}}");

	zassert_equal(msg.uid, 413);
	zassert_equal(msg.action, 0);
	assert_str(msg.app_identifier, "nodomain.freeyourgadget.gadgetb");
	assert_str(msg.title, "Even G1_87");
	assert_str(msg.subtitle, "");
	assert_str(msg.message, "Connected: Battery 98%, 100%, 10%");
	assert_str(msg.date, "2025-11-11 14:25:59");
	assert_str(msg.display_name, "Gadgetbridge");
	zassert_equal(msg.recv_time, 1762899959);
	zassert_equal(msg.start_show_time, 0);
	/* unsent fields are cleared, tails too */
	zassert_equal(msg.pos, 0);
	zassert_equal(msg.sync_state, 0);
	zassert_equal(msg.busy, 0);
	zassert_equal(msg.read, 0);
	zassert_equal(msg.state, 0);
	assert_zero(msg.title + 10, sizeof(msg.title) - 10, "title tail");
	assert_zero(msg.message + 33, sizeof(msg.message) - 33, "message tail");
}

ZTEST(json, test_ncs_strings_longer_than_31_bytes_are_cut_to_31_and_terminated)
{
	parse("{\"ncs_notification\":{"
	      "\"app_identifier\":\"com.example.averyveryverylongpackagename\","
	      "\"title\":\"TITLE-0123456789abcdefghijklmnopqrstuv\","
	      "\"subtitle\":\"SUBTI-0123456789abcdefghijklmnopqrstuv\","
	      "\"display_name\":\"DISPL-0123456789abcdefghijklmnopqrstuv\"}}");

	assert_str(msg.app_identifier, "com.example.averyveryverylongpa");
	assert_str(msg.title, "TITLE-0123456789abcdefghijklmno");
	assert_str(msg.subtitle, "SUBTI-0123456789abcdefghijklmno");
	assert_str(msg.display_name, "DISPL-0123456789abcdefghijklmno");
	zassert_equal(msg.title[31], 0);
}

ZTEST(json, test_ncs_message_longer_than_255_bytes_is_cut_to_255_and_terminated)
{
	static char json[512];
	static char text[301];
	static char want[256];

	for (int i = 0; i < 300; i++) {
		text[i] = 'a' + i % 26;
	}
	text[300] = 0;
	memcpy(want, text, 255);
	want[255] = 0;
	snprintf(json, sizeof(json),
		 "{\"ncs_notification\":{\"message\":\"%s\",\"date\":\"2025-11-11 14:25:59\"}}",
		 text);

	parse(json);

	zassert_equal(strlen(msg.message), 255);
	assert_str(msg.message, want);
	assert_str(msg.date, "2025-11-11 14:25:59");
}

/* DEFECT: strncpy(..., 32) leaves a long date unterminated. */
ZTEST_EXPECT_FAIL(json, test_ncs_date_longer_than_31_bytes_is_cut_to_31_and_terminated);
ZTEST(json, test_ncs_date_longer_than_31_bytes_is_cut_to_31_and_terminated)
{
	parse("{\"ncs_notification\":{\"action\":0,"
	      "\"date\":\"2025-11-11 14:25:59 GMT-08:00 Pacific Standard Time\"}}");

	zassert_equal(msg.date[31], 0, "date[31] is 0x%02x, field not terminated",
		      (uint8_t)msg.date[31]);
	zassert_mem_equal(msg.date, "2025-11-11 14:25:59 GMT-08:00 P", 31);
}

ZTEST(json, test_ncs_missing_keys_leave_zeros)
{
	g1_clock_set_date(1762899959);

	parse("{\"ncs_notification\":{}}");

	ASSERT_ZERO_FIELD(app_identifier);
	ASSERT_ZERO_FIELD(display_name);
	ASSERT_ZERO_FIELD(title);
	ASSERT_ZERO_FIELD(subtitle);
	ASSERT_ZERO_FIELD(message);
	ASSERT_ZERO_FIELD(date);
	zassert_equal(msg.uid, 0);
	zassert_equal(msg.action, 0);
	zassert_equal(msg.start_show_time, 0);
	zassert_equal(msg.pos, 0);
	zassert_equal(msg.sync_state, 0);
	zassert_equal(msg.busy, 0);
	zassert_equal(msg.recv_time, 1762899959);

	parse("{\"ncs_notification\":{\"msg_id\":7,\"title\":\"t\"}}");
	zassert_equal(msg.uid, 7);
	assert_str(msg.title, "t");
	ASSERT_ZERO_FIELD(app_identifier);
	ASSERT_ZERO_FIELD(message);
	ASSERT_ZERO_FIELD(date);
	zassert_equal(msg.action, 0);
}

ZTEST(json, test_ncs_msg_id_and_action_numbers_are_stored)
{
	parse("{\"ncs_notification\":{\"msg_id\":2147483647,\"action\":2}}");

	zassert_equal(msg.uid, 0x7fffffffu);
	zassert_equal(msg.action, 2);

	parse("{\"ncs_notification\":{\"msg_id\":0,\"action\":1}}");
	zassert_equal(msg.uid, 0);
	zassert_equal(msg.action, 1);
}

/* DEFECT: cJSON valueint saturates at INT_MAX, so 0x4C cannot clear such ids. */
ZTEST_EXPECT_FAIL(json, test_ncs_msg_id_above_int32_max_is_stored_as_u32);
ZTEST(json, test_ncs_msg_id_above_int32_max_is_stored_as_u32)
{
	parse("{\"ncs_notification\":{\"msg_id\":3000000000}}");

	zassert_equal(msg.uid, 3000000000u, "uid 0x%08x", msg.uid);
}

ZTEST(json, test_ncs_incoming_call_sets_start_show_time_to_the_receive_date)
{
	g1_clock_set_date(1762899959);

	parse("{\"ncs_notification\":{\"msg_id\":20,\"action\":0,"
	      "\"app_identifier\":\"com.android.phone_incall\",\"title\":\"Alice\","
	      "\"message\":\"Incoming call\"}}");

	assert_str(msg.app_identifier, "com.android.phone_incall");
	zassert_equal(msg.start_show_time, 1762899959);
	zassert_equal(msg.recv_time, 1762899959);

	parse("{\"ncs_notification\":{\"app_identifier\":\"com.android.phone_missed\"}}");
	zassert_equal(msg.start_show_time, 0);
}

/* raw UTF-8, no \u escapes */
ZTEST(json, test_ncs_utf8_title_and_message_from_the_unicode_log_are_copied_byte_exact)
{
	parse("{\"ncs_notification\":{\"msg_id\":732,\"action\":0,"
	      "\"app_identifier\":\"com.hasmetd.notificationnotes\","
	      "\"title\":\"\xec\x83\x9d\",\"subtitle\":\"\","
	      "\"message\":\"\xea\xb3\xa0\xed\x94\xbc test \xeb\xa6\xb4\","
	      "\"time_s\":1763845331,\"date\":\"2025-11-22 13:02:11\","
	      "\"display_name\":\"Notification Notes\"}}");

	zassert_equal(msg.uid, 732);
	assert_str(msg.app_identifier, "com.hasmetd.notificationnotes");
	zassert_mem_equal(msg.title, "\xec\x83\x9d\x00", 4);
	zassert_mem_equal(msg.message, "\xea\xb3\xa0\xed\x94\xbc test \xeb\xa6\xb4\x00", 16);
	assert_str(msg.display_name, "Notification Notes");
}

/* DEFECT: the cut at 31 bytes leaves a UTF-8 lead byte at title[30]. */
ZTEST_EXPECT_FAIL(json, test_ncs_utf8_title_cut_does_not_split_a_character);
ZTEST(json, test_ncs_utf8_title_cut_does_not_split_a_character)
{
	static const char syllable[] = "\xec\x83\x9d"; /* 생 */
	static char json[256];
	static char title[34];
	static char want[31];

	title[0] = 0;
	for (int i = 0; i < 11; i++) {
		strcat(title, syllable);
	}
	memcpy(want, title, 30);
	want[30] = 0;
	snprintf(json, sizeof(json), "{\"ncs_notification\":{\"title\":\"%s\"}}", title);

	parse(json);

	zassert_mem_equal(msg.title, want, 31, "title[30] is 0x%02x", (uint8_t)msg.title[30]);
}

/* e.g. a lost second 0x4B chunk */
ZTEST(json, test_ncs_invalid_json_leaves_the_message_untouched)
{
	parse("{\"ncs_notification\":{\"msg_id\":413,\"action\":0,"
	      "\"app_identifier\":\"nodomain.freeyourgadget.gadgetb\",\"title\":\"Even G1_87\","
	      "\"subtitle\":\"\",\"message\":\"Connected: Battery 98%, 100%, 10%\"");
	assert_untouched();

	parse("ncs_notification");
	assert_untouched();

	parse("");
	assert_untouched();
}

ZTEST(json, test_ncs_missing_ncs_notification_node_leaves_the_message_untouched)
{
	parse("{\"msg_id\":413,\"title\":\"Even G1_87\"}");
	assert_untouched();

	parse("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
	      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}");
	assert_untouched();
}

/* DEFECT: a non-string title passes NULL to strncpy(); skipped as it crashes. */
ZTEST(json, test_ncs_null_title_is_treated_as_missing)
{
	ztest_test_skip();

	parse("{\"ncs_notification\":{\"msg_id\":5,\"title\":null,\"message\":\"m\"}}");

	ASSERT_ZERO_FIELD(title);
	zassert_equal(msg.uid, 5);
	assert_str(msg.message, "m");
}
