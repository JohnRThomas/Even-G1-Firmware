/*
 * Record: flags (call 0, msg 1, ios_mail 2, calendar 3, app.enable 4), count, 100 x id[40] +
 * name[16].
 */
#include "notif_test.h"

/* the apps of the 0x04_whitelist vectors */
static const char *const log_ids[] = {"nodomain.freeyourgadget.gadgetb", "com.discord",
              "com.google.android.gm"};
static const char *const log_names[] = {"Gadgetbridge", "Discord", "Gmail"};

static void assert_log_record(void)
{
  const struct app_whitelist *record = (const void *)get_app_whitelist_info();

  zassert_equal(record->call_enable, 1);
  zassert_equal(record->msg_enable, 1);
  zassert_equal(record->ios_mail_enable, 1);
  zassert_equal(record->calendar_enable, 1);
  zassert_equal(record->app_enable, 1);
  zassert_equal(record->app_count, 3);
  for (int app = 0; app < 3; app++) {
    expect_str(record->list[app].id, log_ids[app]);
    expect_str(record->list[app].name, log_names[app]);
  }
}

static void send_log_whitelist(void)
{
  g1_send_vector("0x04_whitelist_chunk1");
  g1_send_vector("0x04_whitelist_chunk2");
}

ZTEST(notifications, test_0x04_two_chunks_reply_cb_then_c9)
{
  g1_send_vector("0x04_whitelist_chunk1");
  expect_only_frame("04 cb");
  g1_send_vector("0x04_whitelist_chunk2");
  expect_only_frame("04 c9");
}

ZTEST(notifications, test_0x04_whitelist_is_in_use_after_the_last_chunk)
{
  send_log_whitelist();

  assert_log_record();
  zassert_equal(is_on_whitelist_by_identifier("com.discord"), 2);
}

ZTEST(notifications, test_0x04_whitelist_is_written_to_flash_in_the_record_layout)
{
  const uint8_t *record = &g1_fake_flash_image[WHITELIST_FLASH_ADDR];

  send_log_whitelist();

  zassert_equal(write_whitelist_to_flash_fake.call_count, 1);
  zassert_equal(write_whitelist_to_flash_fake.arg0_val, 0x131000);
  zassert_equal(write_whitelist_to_flash_fake.arg2_val, 0x15e2);
  zassert_equal(record[0], 0x1f, "flags 0x%02x", record[0]);
  zassert_equal(record[1], 3);
  for (int app = 0; app < 3; app++) {
    expect_str((const char *)record + 2 + app * 56, log_ids[app]);
    expect_str((const char *)record + 2 + app * 56 + 40, log_names[app]);
  }
  expect_fill(record + 2 + 3 * 56, 0x15e2 - (2 + 3 * 56), 0, "record after the 3 apps");
}

ZTEST(notifications, test_0x2e_reads_the_stored_whitelist_back_in_17_byte_parts)
{
  static char text[0x1800];
  size_t len = 0;
  int count;
  cJSON *root, *app, *list;

  send_log_whitelist();
  g1_frames_clear();

  g1_send_vector("0x2e_whitelist_get");

  count = g1_frame_count();
  zassert_true(count > 1, "%d frames", count);
  for (int index = 0; index < count; index++) {
    size_t frame_len;
    const uint8_t *frame = g1_frame_peek(index, &frame_len);

    zassert_equal(frame[0], 0x2e, "frame %d", index);
    zassert_equal(frame[1], count, "frame %d count %u, %d frames", index, frame[1],
            count);
    zassert_equal(frame[2], index, "frame %d index %u", index, frame[2]);
    if (index + 1 < count) {
      zassert_equal(frame_len, 20, "frame %d is %u bytes", index,
              (unsigned int)frame_len);
    } else {
      zassert_true(frame_len >= 3 && frame_len < 20, "last frame %u bytes",
             (unsigned int)frame_len);
    }
    memcpy(text + len, frame + 3, frame_len - 3);
    len += frame_len - 3;
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
  expect_str(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 0), "id")->valuestring,
       "nodomain.freeyourgadget.gadgetb");
  expect_str(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 1), "name")->valuestring,
       "Discord");
  expect_str(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 2), "id")->valuestring,
       "com.google.android.gm");
  cJSON_Delete(root);
}

ZTEST(notifications, test_0x04_whitelist_is_loaded_from_flash_after_a_reboot)
{
  send_log_whitelist();

  g1_unit_reboot();

  assert_log_record();
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
  G1_FAKE_RETURN(write_whitelist_to_flash, -EIO);

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
  uint8_t pkt[200] = {0x04, 1, 0};

  send_log_whitelist();
  g1_frames_clear();
  memcpy(&pkt[3], json, strlen(json));
  g1_send(pkt, 3 + strlen(json));

  expect_frame("04 ca");
  zassert_equal(write_whitelist_to_flash_fake.call_count, 1);
  g1_unit_reboot();
  assert_log_record();
}
