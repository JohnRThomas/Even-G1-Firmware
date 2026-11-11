/*
 * Record: flags (call 0, msg 1, ios_mail 2, calendar 3, app.enable 4), count, 100 x id[40] +
 * name[16].
 */
#include <cJSON.h>

#include "notif_test.h"
#include "fakes_storage.h"

static const uint8_t *flash_record(void)
{
	return &g1_fake_flash_image[WHITELIST_FLASH_ADDR];
}

static struct app_whitelist *wl(void)
{
	return (struct app_whitelist *)get_app_whitelist_info();
}

static void assert_log_record(const struct app_whitelist *r)
{
	zassert_equal(r->call_enable, 1);
	zassert_equal(r->msg_enable, 1);
	zassert_equal(r->ios_mail_enable, 1);
	zassert_equal(r->calendar_enable, 1);
	zassert_equal(r->app_enable, 1);
	zassert_equal(r->app_count, 3);
	assert_str(r->list[0].id, "nodomain.freeyourgadget.gadgetb");
	assert_str(r->list[0].name, "Gadgetbridge");
	assert_str(r->list[1].id, "com.discord");
	assert_str(r->list[1].name, "Discord");
	assert_str(r->list[2].id, "com.google.android.gm");
	assert_str(r->list[2].name, "Gmail");
}

static void send_log_whitelist(void)
{
	g1_send_vector("0x04_whitelist_chunk1");
	g1_send_vector("0x04_whitelist_chunk2");
}

ZTEST(notifications, test_0x04_two_chunks_reply_cb_then_c9)
{
	g1_send_vector("0x04_whitelist_chunk1");
	expect_frame("04 cb");
	expect_no_frame();
	g1_send_vector("0x04_whitelist_chunk2");
	expect_frame("04 c9");
	expect_no_frame();
}

ZTEST(notifications, test_0x04_whitelist_is_in_use_after_the_last_chunk)
{
	send_log_whitelist();

	assert_log_record(wl());
	zassert_equal(is_on_whitelist_by_identifier("com.discord"), 2);
}

ZTEST(notifications, test_0x04_whitelist_is_written_to_flash_in_the_record_layout)
{
	const uint8_t *f = flash_record();

	send_log_whitelist();

	zassert_equal(write_whitelist_to_flash_fake.call_count, 1);
	zassert_equal(write_whitelist_to_flash_fake.arg0_val, 0x131000);
	zassert_equal(write_whitelist_to_flash_fake.arg2_val, 0x15e2);
	zassert_equal(f[0], 0x1f, "flags 0x%02x", f[0]);
	zassert_equal(f[1], 3);
	assert_str((const char *)f + 2, "nodomain.freeyourgadget.gadgetb");
	assert_str((const char *)f + 2 + 40, "Gadgetbridge");
	assert_str((const char *)f + 2 + 56, "com.discord");
	assert_str((const char *)f + 2 + 56 + 40, "Discord");
	assert_str((const char *)f + 2 + 112, "com.google.android.gm");
	assert_str((const char *)f + 2 + 112 + 40, "Gmail");
	for (size_t i = 2 + 3 * 56; i < 0x15e2; i++) {
		zassert_equal(f[i], 0, "record byte 0x%x is 0x%02x", (unsigned int)i, f[i]);
	}
}

ZTEST(notifications, test_0x2e_reads_the_stored_whitelist_back_in_17_byte_parts)
{
	static char text[0x1800];
	size_t len = 0;
	int n;
	cJSON *root, *app, *list;

	send_log_whitelist();
	g1_frames_clear();

	g1_send_vector("0x2e_whitelist_get");

	n = g1_frame_count();
	zassert_true(n > 1, "%d frames", n);
	for (int i = 0; i < n; i++) {
		size_t flen;
		const uint8_t *fr = g1_frame_peek(i, &flen);

		zassert_equal(fr[0], 0x2e, "frame %d", i);
		zassert_equal(fr[1], n, "frame %d count %u, %d frames", i, fr[1], n);
		zassert_equal(fr[2], i, "frame %d index %u", i, fr[2]);
		if (i + 1 < n) {
			zassert_equal(flen, 20, "frame %d is %u bytes", i, (unsigned int)flen);
		} else {
			zassert_true(flen >= 3 && flen < 20, "last frame %u bytes", (unsigned int)flen);
		}
		memcpy(text + len, fr + 3, flen - 3);
		len += flen - 3;
	}
	text[len] = 0;
	zassert_equal(strlen(text), len, "NUL inside the text");

	root = cJSON_Parse(text);
	zassert_not_null(root, "not JSON: %s", text);
	zassert_true(cJSON_IsTrue(cJSON_GetObjectItem(root, "call_enable")), "%s", text);
	zassert_true(cJSON_IsTrue(cJSON_GetObjectItem(root, "msg_enable")), "%s", text);
	zassert_true(cJSON_IsTrue(cJSON_GetObjectItem(root, "ios_mail_enable")), "%s", text);
	zassert_true(cJSON_IsTrue(cJSON_GetObjectItem(root, "calendar_enable")), "%s", text);
	app = cJSON_GetObjectItem(root, "app");
	zassert_true(cJSON_IsTrue(cJSON_GetObjectItem(app, "enable")), "%s", text);
	list = cJSON_GetObjectItem(app, "list");
	zassert_equal(cJSON_GetArraySize(list), 3, "%s", text);
	assert_str(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 0), "id")->valuestring,
		   "nodomain.freeyourgadget.gadgetb");
	assert_str(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 1), "name")->valuestring,
		   "Discord");
	assert_str(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 2), "id")->valuestring,
		   "com.google.android.gm");
	cJSON_Delete(root);
}

ZTEST(notifications, test_0x04_whitelist_is_loaded_from_flash_after_a_reboot)
{
	send_log_whitelist();

	g1_unit_reboot();

	assert_log_record(wl());
	zassert_equal(is_on_whitelist_by_identifier("com.google.android.gm"), 2);
}

/* the app resends it at every settings sync */
ZTEST(notifications, test_0x04_same_whitelist_again_is_not_rewritten)
{
	send_log_whitelist();
	g1_frames_clear();

	send_log_whitelist();

	expect_frame("04 cb");
	expect_frame("04 c9");
	zassert_equal(write_whitelist_to_flash_fake.call_count, 1);
}

ZTEST(notifications, test_0x04_failed_flash_write_replies_ca)
{
	write_whitelist_to_flash_fake.custom_fake = NULL;
	write_whitelist_to_flash_fake.return_val = -EIO;

	send_log_whitelist();

	expect_frame("04 cb");
	expect_frame("04 ca");
	zassert_equal(write_whitelist_to_flash_fake.call_count, 3);
	printk("\n"); /* the store failure log line has no newline */
}

ZTEST(notifications, test_0x04_whitelist_missing_a_key_replies_ca_and_keeps_the_flash)
{
	static const char json[] =
		"{\"calendar_enable\":true,\"call_enable\":true,\"ios_mail_enable\":true,"
		"\"app\":{\"list\":[],\"enable\":true}}";
	uint8_t pkt[200];

	send_log_whitelist();
	g1_frames_clear();
	pkt[0] = 0x04;
	pkt[1] = 1;
	pkt[2] = 0;
	memcpy(&pkt[3], json, strlen(json));
	g1_send(pkt, 3 + strlen(json));

	expect_frame("04 ca");
	zassert_equal(write_whitelist_to_flash_fake.call_count, 1);
	g1_unit_reboot();
	assert_log_record(wl());
}
