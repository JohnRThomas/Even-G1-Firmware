/* Lookup returns 0 invalid, 1 blocked, 2 allowed. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <cJSON.h>

#include "harness.h"
#include "notify/whitelist.h"

/* no prototype in whitelist.h */
uint16_t convert_app_whitelist_to_json(char *buf);
int is_app_name_same(const char *id, const char *name);

#define assert_str(got, want)                                                                      \
	zassert_true(strcmp((got), (want)) == 0, "%s is \"%s\", expected \"%s\"", #got, (got),     \
		     (want))

static struct app_whitelist *wl(void)
{
	return (struct app_whitelist *)get_app_whitelist_info();
}

static const char log_json[] =
	"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
	"\"ios_mail_enable\":true,\"app\":{\"list\":["
	"{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
	"{\"id\":\"com.discord\",\"name\":\"Discord\"},"
	"{\"id\":\"com.google.android.gm\",\"name\":\"Gmail\"}],\"enable\":true}}";

static const char *const log_ids[] = {"nodomain.freeyourgadget.gadgetb", "com.discord",
				      "com.google.android.gm"};
static const char *const log_names[] = {"Gadgetbridge", "Discord", "Gmail"};

static char out[0x1800];
static char big[0x1800];

static void assert_flags(int call, int msg, int ios_mail, int calendar, int app)
{
	zassert_equal(wl()->call_enable, call, "call_enable %d", wl()->call_enable);
	zassert_equal(wl()->msg_enable, msg, "msg_enable %d", wl()->msg_enable);
	zassert_equal(wl()->ios_mail_enable, ios_mail, "ios_mail_enable %d",
		      wl()->ios_mail_enable);
	zassert_equal(wl()->calendar_enable, calendar, "calendar_enable %d",
		      wl()->calendar_enable);
	zassert_equal(wl()->app_enable, app, "app_enable %d", wl()->app_enable);
}

static void assert_log_record(void)
{
	assert_flags(1, 1, 1, 1, 1);
	zassert_equal(wl()->app_count, 3);
	for (int i = 0; i < 3; i++) {
		assert_str(wl()->list[i].id, log_ids[i]);
		assert_str(wl()->list[i].name, log_names[i]);
	}
}

/* -1 when missing or not a bool */
static int json_bool(const cJSON *obj, const char *key)
{
	const cJSON *item = cJSON_GetObjectItem(obj, key);

	if (cJSON_IsTrue(item)) {
		return 1;
	}
	if (cJSON_IsFalse(item)) {
		return 0;
	}
	return -1;
}

static void assert_whitelist_json(const char *text, int call, int msg, int ios_mail,
				  int calendar, int app_enable, int n, const char *const ids[],
				  const char *const names[])
{
	cJSON *root = cJSON_Parse(text);
	cJSON *app;
	cJSON *list;

	zassert_not_null(root, "not JSON: %s", text);
	zassert_equal(json_bool(root, "call_enable"), call, "%s", text);
	zassert_equal(json_bool(root, "msg_enable"), msg, "%s", text);
	zassert_equal(json_bool(root, "ios_mail_enable"), ios_mail, "%s", text);
	zassert_equal(json_bool(root, "calendar_enable"), calendar, "%s", text);
	app = cJSON_GetObjectItem(root, "app");
	zassert_not_null(app, "%s", text);
	zassert_equal(json_bool(app, "enable"), app_enable, "%s", text);
	list = cJSON_GetObjectItem(app, "list");
	zassert_true(cJSON_IsArray(list), "%s", text);
	zassert_equal(cJSON_GetArraySize(list), n, "%s", text);
	for (int i = 0; i < n; i++) {
		cJSON *e = cJSON_GetArrayItem(list, i);
		cJSON *id = cJSON_GetObjectItem(e, "id");
		cJSON *name = cJSON_GetObjectItem(e, "name");

		zassert_true(cJSON_IsString(id) && strcmp(id->valuestring, ids[i]) == 0,
			     "app %d id in %s", i, text);
		zassert_true(cJSON_IsString(name) && strcmp(name->valuestring, names[i]) == 0,
			     "app %d name in %s", i, text);
	}
	cJSON_Delete(root);
}

ZTEST(json, test_whitelist_from_the_app_log_is_parsed_into_the_record)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);
	assert_log_record();
}

ZTEST(json, test_whitelist_round_trips_to_the_0x2e_json)
{
	int len;

	zassert_equal(parse_app_white_list_json(log_json), 1);
	memset(out, 0, sizeof(out));

	len = convert_app_whitelist_to_json(out);

	zassert_equal(len, strlen(out), "returned %d for %u characters", len,
		      (unsigned int)strlen(out));
	assert_whitelist_json(out, 1, 1, 1, 1, 1, 3, log_ids, log_names);

	memset(wl(), 0, sizeof(*wl()));
	zassert_equal(parse_app_white_list_json(out), 1);
	assert_log_record();
	zassert_equal(is_the_same_whitelist_app(log_json), 1);
}

ZTEST(json, test_whitelist_false_flags_and_empty_list_round_trip)
{
	static const char json[] = "{\"calendar_enable\":false,\"call_enable\":true,"
				   "\"msg_enable\":false,\"ios_mail_enable\":true,"
				   "\"app\":{\"list\":[],\"enable\":false}}";

	zassert_equal(parse_app_white_list_json(json), 1);
	assert_flags(1, 0, 1, 0, 0);
	zassert_equal(wl()->app_count, 0);

	memset(out, 0, sizeof(out));
	zassert_equal(convert_app_whitelist_to_json(out), strlen(out));
	assert_whitelist_json(out, 1, 0, 1, 0, 0, 0, NULL, NULL);
}

ZTEST(json, test_whitelist_without_call_enable_is_rejected)
{
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}"),
		      0);
}

ZTEST(json, test_whitelist_without_msg_enable_is_rejected)
{
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}"),
		      0);
}

ZTEST(json, test_whitelist_without_any_other_key_is_rejected)
{
	zassert_equal(parse_app_white_list_json(
			      "{\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}"),
		      0);
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,"
			      "\"msg_enable\":true,\"app\":{\"list\":[],\"enable\":true}}"),
		      0);
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,"
			      "\"msg_enable\":true,\"ios_mail_enable\":true}"),
		      0);
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":[]}}"),
		      0);
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"enable\":true}}"),
		      0);
}

/* DEFECT: the record is cleared before the keys are checked. */
ZTEST_EXPECT_FAIL(json, test_whitelist_rejected_for_a_missing_key_keeps_the_previous_one);
ZTEST(json, test_whitelist_rejected_for_a_missing_key_keeps_the_previous_one)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":false,\"call_enable\":false,"
			      "\"ios_mail_enable\":false,\"app\":{\"list\":[],\"enable\":false}}"),
		      0);

	assert_log_record();
}

ZTEST(json, test_whitelist_invalid_json_is_rejected_and_keeps_the_previous_one)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	/* the first 0x04 chunk alone */
	memcpy(big, log_json, 177);
	big[177] = 0;
	zassert_equal(parse_app_white_list_json(big), 0);
	zassert_equal(parse_app_white_list_json("garbage"), 0);

	assert_log_record();
}

ZTEST(json, test_whitelist_with_more_apps_than_the_record_holds_keeps_the_first_100)
{
	int pos;

	pos = snprintf(big, sizeof(big),
		       "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		       "\"ios_mail_enable\":true,\"app\":{\"list\":[");
	for (int i = 0; i < 101; i++) {
		pos += snprintf(big + pos, sizeof(big) - pos,
				"%s{\"id\":\"com.example.app%03d\",\"name\":\"App %03d\"}",
				i ? "," : "", i, i);
	}
	pos += snprintf(big + pos, sizeof(big) - pos, "],\"enable\":true}}");
	zassert_true(pos < 0x1800, "JSON of %d bytes", pos);

	zassert_equal(parse_app_white_list_json(big), 1);

	zassert_equal(wl()->app_count, 100);
	assert_str(wl()->list[0].id, "com.example.app000");
	assert_str(wl()->list[0].name, "App 000");
	assert_str(wl()->list[99].id, "com.example.app099");
	assert_str(wl()->list[99].name, "App 099");
	zassert_equal(is_on_whitelist_by_identifier("com.example.app100"), 1);
	zassert_equal(is_on_whitelist_by_identifier("com.example.app099"), 2);
}

/* DEFECT: app_count counts skipped entries; an empty id matches every app. */
ZTEST_EXPECT_FAIL(json, test_whitelist_app_entry_without_a_name_is_dropped);
ZTEST(json, test_whitelist_app_entry_without_a_name_is_dropped)
{
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"com.noname\"},"
			      "{\"id\":\"com.discord\",\"name\":\"Discord\"}],\"enable\":true}}"),
		      1);

	assert_str(wl()->list[0].id, "com.discord");
	zassert_equal(is_on_whitelist_by_identifier("com.unlisted.app"), 1);
	zassert_equal(wl()->app_count, 1, "app_count %d", wl()->app_count);
}

ZTEST(json, test_whitelist_ids_and_names_are_cut_to_39_and_15_characters)
{
	zassert_equal(parse_app_white_list_json(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"com.example.a.package.name.of.43.characters\","
			      "\"name\":\"Notification Notes Pro\"}],\"enable\":true}}"),
		      1);

	zassert_equal(wl()->app_count, 1);
	assert_str(wl()->list[0].id, "com.example.a.package.name.of.43.charac");
	assert_str(wl()->list[0].name, "Notification No");
}

ZTEST(json, test_whitelist_lookup_rejects_null_and_identifiers_of_40_characters_or_more)
{
	wl()->app_enable = 1;
	wl()->app_count = 1;
	strcpy(wl()->list[0].id, "com.example");

	zassert_equal(is_on_whitelist_by_identifier(NULL), 0);
	zassert_equal(is_on_whitelist_by_identifier("com.example.0123456789abcdefghijklmnopqr"), 0);
	zassert_equal(is_on_whitelist_by_identifier("com.example.0123456789abcdefghijklmnopq"), 2);
}

ZTEST(json, test_whitelist_lookup_call_identifiers_follow_call_enable)
{
	static const char *const ids[] = {"com.apple.mobilephone", "com.apple.mobilephone_missed",
					  "com.android.phone_incall", "com.android.phone_missed"};

	for (int i = 0; i < ARRAY_SIZE(ids); i++) {
		zassert_equal(is_on_whitelist_by_identifier(ids[i]), 1, "%s blocked", ids[i]);
	}
	wl()->call_enable = 1;
	for (int i = 0; i < ARRAY_SIZE(ids); i++) {
		zassert_equal(is_on_whitelist_by_identifier(ids[i]), 2, "%s allowed", ids[i]);
	}
}

ZTEST(json, test_whitelist_lookup_sms_identifiers_follow_msg_enable)
{
	static const char *const ids[] = {"com.apple.MobileSMS", "com.android.even_sms"};

	wl()->call_enable = 1;
	wl()->calendar_enable = 1;
	for (int i = 0; i < ARRAY_SIZE(ids); i++) {
		zassert_equal(is_on_whitelist_by_identifier(ids[i]), 1, "%s blocked", ids[i]);
	}
	wl()->msg_enable = 1;
	for (int i = 0; i < ARRAY_SIZE(ids); i++) {
		zassert_equal(is_on_whitelist_by_identifier(ids[i]), 2, "%s allowed", ids[i]);
	}
}

ZTEST(json, test_whitelist_lookup_calendar_identifiers_follow_calendar_enable)
{
	static const char *const ids[] = {"com.apple.mobilecal", "com.android.even_calendar"};

	wl()->msg_enable = 1;
	for (int i = 0; i < ARRAY_SIZE(ids); i++) {
		zassert_equal(is_on_whitelist_by_identifier(ids[i]), 1, "%s blocked", ids[i]);
	}
	wl()->calendar_enable = 1;
	for (int i = 0; i < ARRAY_SIZE(ids); i++) {
		zassert_equal(is_on_whitelist_by_identifier(ids[i]), 2, "%s allowed", ids[i]);
	}
}

ZTEST(json, test_whitelist_lookup_ios_mail_follows_ios_mail_enable)
{
	wl()->msg_enable = 1;
	zassert_equal(is_on_whitelist_by_identifier("com.apple.mobilemail"), 1);
	wl()->ios_mail_enable = 1;
	zassert_equal(is_on_whitelist_by_identifier("com.apple.mobilemail"), 2);
}

ZTEST(json, test_whitelist_lookup_listed_app_is_allowed_only_with_app_enable)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(is_on_whitelist_by_identifier("com.discord"), 2);
	zassert_equal(is_on_whitelist_by_identifier("nodomain.freeyourgadget.gadgetb"), 2);
	zassert_equal(is_on_whitelist_by_identifier("com.whatsapp"), 1);

	wl()->app_enable = 0;
	zassert_equal(is_on_whitelist_by_identifier("com.discord"), 1);
}

/* prefix match on strlen(id), not a substring search */
ZTEST(json, test_whitelist_lookup_listed_id_allows_identifiers_that_start_with_it)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(is_on_whitelist_by_identifier("nodomain.freeyourgadget.gadgetbridge"), 2);
	zassert_equal(is_on_whitelist_by_identifier("com.google.android.gm.lite"), 2);
	zassert_equal(is_on_whitelist_by_identifier("com.google.android"), 1);
	zassert_equal(is_on_whitelist_by_identifier("org.com.discord"), 1);
}

ZTEST(json, test_whitelist_same_apps_in_another_order_is_the_same)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"com.google.android.gm\",\"name\":\"Gmail\"},"
			      "{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
			      "{\"id\":\"com.discord\",\"name\":\"Discord\"}],\"enable\":true}}"),
		      1);
}

ZTEST(json, test_whitelist_changed_flag_is_not_the_same)
{
	static const char *const changed[] = {
		"{\"calendar_enable\":false,\"call_enable\":true,\"msg_enable\":true,"
		"\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}",
		"{\"calendar_enable\":true,\"call_enable\":false,\"msg_enable\":true,"
		"\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}",
		"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":false,"
		"\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}",
		"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		"\"ios_mail_enable\":false,\"app\":{\"list\":[],\"enable\":true}}",
		"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		"\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":false}}",
	};
	static const char same[] =
		"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		"\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}";

	zassert_equal(parse_app_white_list_json(same), 1);
	zassert_equal(is_the_same_whitelist_app(same), 1);

	for (int i = 0; i < ARRAY_SIZE(changed); i++) {
		zassert_equal(is_the_same_whitelist_app(changed[i]), 0, "flag %d changed", i);
	}
}

ZTEST(json, test_whitelist_renamed_added_or_removed_app_is_not_the_same)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	/* Discord renamed */
	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
			      "{\"id\":\"com.discord\",\"name\":\"Discord Beta\"},"
			      "{\"id\":\"com.google.android.gm\",\"name\":\"Gmail\"}],\"enable\":true}}"),
		      0);
	/* same name, other id */
	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
			      "{\"id\":\"com.discord\",\"name\":\"Discord\"},"
			      "{\"id\":\"com.google.android.gm.lite\",\"name\":\"Gmail\"}],"
			      "\"enable\":true}}"),
		      0);
	/* a fourth app */
	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
			      "{\"id\":\"com.discord\",\"name\":\"Discord\"},"
			      "{\"id\":\"com.google.android.gm\",\"name\":\"Gmail\"},"
			      "{\"id\":\"com.whatsapp\",\"name\":\"WhatsApp\"}],\"enable\":true}}"),
		      0);
	/* Gmail removed */
	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":["
			      "{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
			      "{\"id\":\"com.discord\",\"name\":\"Discord\"}],\"enable\":true}}"),
		      0);
}

ZTEST(json, test_whitelist_invalid_or_incomplete_json_is_not_the_same)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(is_the_same_whitelist_app("garbage"), 0);
	/* no call_enable */
	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":[],\"enable\":true}}"),
		      0);
	/* list not an array */
	zassert_equal(is_the_same_whitelist_app(
			      "{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
			      "\"ios_mail_enable\":true,\"app\":{\"list\":{},\"enable\":true}}"),
		      0);
}

/* else every 0x04 rewrites the flash */
ZTEST(json, test_whitelist_with_long_ids_and_names_is_the_same_after_storing_it)
{
	static const char json[] =
		"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
		"\"ios_mail_enable\":true,\"app\":{\"list\":["
		"{\"id\":\"com.example.a.package.name.of.43.characters\","
		"\"name\":\"Notification Notes Pro\"}],\"enable\":true}}";

	zassert_equal(parse_app_white_list_json(json), 1);

	zassert_equal(is_the_same_whitelist_app(json), 1);
}

ZTEST(json, test_app_name_same_needs_the_id_and_name_of_one_entry)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(is_app_name_same("com.discord", "Discord"), 1);
	zassert_equal(is_app_name_same("com.google.android.gm", "Gmail"), 1);
	zassert_equal(is_app_name_same("com.discord", "Gmail"), 0);
	zassert_equal(is_app_name_same("com.whatsapp", "WhatsApp"), 0);
	zassert_equal(is_app_name_same("com.discord.beta", "Discord"), 0);
	zassert_equal(is_app_name_same("com.discord", "Disc"), 0);
}

ZTEST(json, test_app_name_same_rejects_null_arguments)
{
	zassert_equal(parse_app_white_list_json(log_json), 1);

	zassert_equal(is_app_name_same(NULL, "Discord"), 0);
	zassert_equal(is_app_name_same("com.discord", NULL), 0);
}
