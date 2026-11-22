/*
 * Phone packet routing: PUT 0x01..0x27, GET 0x29..0x45, POST 0x47..0x56, report 0xF1..0xF6, else
 * "NN". '#' debug strings that match no debug command reach the dispatcher as unknown PUT 0x23.
 */
#include <app_version.h>

#include "fakes.h"
#include "harness.h"
#include "notify/whitelist.h"

ZTEST_SUITE(proto_dispatch, NULL, NULL, NULL, NULL, NULL);

static void send_byte(uint8_t id) {
  g1_send(&id, 1);
}

/* Unknown id in a handler's range: cmd, 0xCA, NUL-terminated text, zero-padded to 20. */
static const uint8_t *error_reply(uint8_t cmd, const char *text) {
  static uint8_t reply[20];

  zassert_true(strlen(text) + 1 <= sizeof(reply) - 2, "text too long");
  memset(reply, 0, sizeof(reply));
  reply[0] = cmd;
  reply[1] = 0xca;
  memcpy(&reply[2], text, strlen(text) + 1);
  return reply;
}

#define expect_error_reply(cmd, text) expect_frame_bytes(error_reply(cmd, text), 20)

/* 0x11 has no case in ble_process_put_req; 0x45 ends the GET range. */
ZTEST(proto_dispatch, test_unknown_ids_inside_a_range_get_the_range_error) {
  static const struct {
    const char *ids;
    const char *text;
  } ranges[] = {
    {"0c 11 12 13 19 1a 1b 1d 21", "error put req."},
    {"2f 30 31 40 41 42 43 44 45", "error get req."},
    {"48", "error post req."},
  };

  for (size_t range = 0; range < ARRAY_SIZE(ranges); range++) {
    uint8_t ids[16];
    int count = g1_hex(ranges[range].ids, ids, sizeof(ids));

    for (int index = 0; index < count; index++) {
      send_byte(ids[index]);
      expect_error_reply(ids[index], ranges[range].text);
    }
  }
  expect_no_frame();
}

/* Byte 0 is covered by the test below. */
ZTEST(proto_dispatch, test_report_range_ends_at_0xf6) {
  const uint8_t *want = error_reply(0xf6, "error post req.");
  size_t len;
  const uint8_t *frame;

  send_byte(0xf6);

  frame = g1_frame_peek(0, &len);
  zassert_not_null(frame, "no reply");
  zassert_equal(len, 20, "reply of %u bytes", (unsigned int)len);
  zassert_mem_equal(frame + 1, want + 1, 19);
}

ZTEST(proto_dispatch, test_unknown_report_ids_echo_the_command_in_the_error_reply) {
  static const uint8_t ids[] = {0xf2, 0xf3, 0xf6};

  for (size_t index = 0; index < ARRAY_SIZE(ids); index++) {
    send_byte(ids[index]);
    expect_error_reply(ids[index], "error post req.");
  }
}

ZTEST(proto_dispatch, test_ids_outside_every_range_reply_nn) {
  for (int id = 0x00; id <= 0xff; id++) {
    bool unhandled = id == 0x00 || id == 0x28 || id == 0x46 || (id >= 0x57 && id <= 0xf0) || id >= 0xf7;

    if (!unhandled) {
      continue;
    }
    send_byte(id);
    expect_frame_msg("4e 4e 00", -1, "id 0x%02x", id);
    expect_no_frame_msg("id 0x%02x", id);
  }
}

ZTEST(proto_dispatch, test_packet_of_255_bytes_is_dispatched_and_answered) {
  uint8_t pkt[255] = {0x3b};

  g1_send(pkt, sizeof(pkt));

  expect_frame_padded("3b c9 00 00", 20);
}

ZTEST(proto_dispatch, test_replies_are_20_byte_frames_padded_with_zeros) {
  g1_state()->check_mode = 1;

  /* MACs unset (0xff) */
  g1_send_vector("0x2d_mac_get");
  expect_frame_padded("2d 67 ff ff ff ff ff ff ff ff ff ff ff ff", 20);

  g1_send_vector("0x3a_wear_detection_get");
  expect_frame_padded("3a c9 01", 20);

  g1_send_vector("0x0e_mic_off"); /* PUT */
  expect_frame_padded("0e c9 00", 20);

  g1_send_vector("0x4d_mtu_251"); /* POST */
  expect_only_frame_padded("4d c9", 20);
}

ZTEST(proto_dispatch, test_zero_length_packet_is_ignored) {
  uint8_t none[1] = {0};

  g1_send_vector("0x01_brightness");
  g1_frames_clear();
  g1_send(none, 0);
  expect_no_frame();
}

ZTEST(proto_dispatch, test_0x4a_dfu_bit_without_a_dfu_callback_is_safe) {
  g1_send_hex("4a 01");
  expect_frame_padded("4a c9", 20);
}

ZTEST(proto_dispatch, test_0x23_c_followed_by_more_text_is_not_an_unpair) {
  g1_send_hex("23 63 31");

  zassert_equal(bt_unpair_fake.call_count, 0);
  expect_only_frame_bytes(error_reply(0x23, "error put req."), 20);
}

/* The hook's app is put on the whitelist: an unlisted one is announced with F6 frames as well. */
ZTEST(proto_dispatch, test_0x23_1_title_message_goes_through_the_0x4b_path) {
  const struct notif_msg *msg = (const struct notif_msg *)&g1_ble()->tx_buf[4];
  int count;

  zassert_equal(parse_app_white_list_json("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
                                          "\"ios_mail_enable\":true,\"app\":{\"list\":["
                                          "{\"id\":\"com.tencent.xin\",\"name\":\"WeChat\"}],\"enable\":true}}"),
                1);
  g1_send((const uint8_t *)"#1@title@msg", strlen("#1@title@msg"));

  count = g1_frame_count();
  zassert_true(count >= 2, "%d reply frames", count);
  for (int index = 0; index < count; index++) {
    size_t len;
    const uint8_t *frame = g1_frame_peek(index, &len);
    uint8_t want = index == count - 1 ? 0xc9 : 0xcb;

    zassert_true(len >= 2 && frame[0] == 0x4b, "frame %d: %02x", index, frame[0]);
    zassert_equal(frame[1], want, "chunk %d of %d: status %02x, want %02x", index + 1, count, frame[1], want);
  }
  expect_str(msg->title, "title");
  expect_str(msg->message, "msg");
}

ZTEST(proto_dispatch, test_0x23_1_without_second_at_stays_inside_the_packet) {
  g1_send((const uint8_t *)"#1@no second at", strlen("#1@no second at"));
  zassert_true(g1_frame_count() <= 32);
}

ZTEST(proto_dispatch, test_0x23_1_long_title_and_message_are_cut) {
  char pkt[201];

  memset(pkt, 'm', sizeof(pkt) - 1);
  memcpy(pkt, "#1@", 3);
  memset(pkt + 3, 't', 40);
  pkt[43] = '@';
  g1_send((const uint8_t *)pkt, sizeof(pkt) - 1);
  zassert_true(g1_frame_count() > 0);
}

ZTEST(proto_dispatch, test_0x23_cmd_with_long_command_number_stays_inside_the_buffer) {
  g1_send((const uint8_t *)"#cmd@123456789@1", strlen("#cmd@123456789@1"));
}

ZTEST(proto_dispatch, test_0x23_t_replies_200_bytes_of_build_info) {
  uint8_t want[200] = {0};

  strcpy(net_build_time, "2025-10-22 14:21:16");
  JDB_PANEL_ID = 0x4010;
  snprintf((char *)want, sizeof(want),
           "net build time: 2025-10-22 14:21:16, app build time %s, ver %d.%d.%d, "
           "JBD DeviceID 4010\n\r",
           G1_BUILD_TIME, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL);

  g1_send_vector("0x23_build_info");

  expect_only_frame_bytes(want, sizeof(want));
}

ZTEST(proto_dispatch, test_0x23_r_reboots_without_a_reply) {
  g1_send_vector("0x23_reboot");

  zassert_equal(system_reboot_delayed_fake.call_count, 1);
  expect_no_frame();
}

/* 255 is the longest NUS packet (uint8_t len). */
ZTEST(proto_dispatch, test_0x23_unterminated_255_byte_string_is_an_unknown_put) {
  uint8_t pkt[255];

  memset(pkt, 'x', sizeof(pkt));
  pkt[0] = '#';

  g1_send(pkt, sizeof(pkt));

  expect_only_frame_bytes(error_reply(0x23, "error put req."), 20);
}

ZTEST(proto_dispatch, test_0x23_t_in_an_unterminated_255_byte_string_replies_build_info) {
  uint8_t pkt[255];
  size_t len;
  const uint8_t *frame;

  memset(pkt, 'x', sizeof(pkt));
  pkt[0] = '#';
  pkt[1] = 't';

  g1_send(pkt, sizeof(pkt));

  frame = g1_frame_peek(0, &len);
  zassert_not_null(frame, "no reply");
  zassert_equal(len, 200);
  zassert_mem_equal(frame, "net build time", strlen("net build time"));
}

ZTEST(proto_dispatch, test_0x23_256_byte_packet_is_read_inside_the_buffer) {
  uint8_t pkt[256];

  memset(pkt, 'x', sizeof(pkt));
  pkt[0] = '#';
  g1_send(pkt, sizeof(pkt));
  expect_error_reply(0x23, "error put req.");
}

static int spi_init_stub(struct spi_master_ctx *ctx) {
  ARG_UNUSED(ctx);
  return 0;
}

ZTEST(proto_dispatch, test_0x23_j_does_not_call_an_unset_flash_callback) {
  /* set at boot by spi_master_register_context(), which this suite does not link */
  g1_state()->spi.init_cb = spi_init_stub;

  g1_send_hex("23 6a 31");
  expect_frame_padded("69 6e 69 74 20 66 6c 61 73 68", 20);
}

/* "#2@" in an app name must not turn a whitelist chunk into a head-up. */
ZTEST(proto_dispatch, test_debug_marker_inside_a_packet_is_not_a_debug_command) {
  uint8_t pkt[256];
  int len = g1_vector_load("0x04_whitelist_chunk2", pkt, sizeof(pkt));

  zassert_true(len > 24, "vector: %d", len);
  zassert_mem_equal(&pkt[16], "Discord", 7, "vector layout changed");
  memcpy(&pkt[16], "Di#2@1d", 7);

  g1_send_vector("0x04_whitelist_chunk1");
  expect_frame("04 cb");
  g1_send(pkt, len);

  zassert_equal(trigger_imu_state_change_fake.call_count, 0, "head-up simulated");
  expect_only_frame("04 c9");
}
