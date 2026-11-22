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
static void whitelist_hook_app(void) {
  zassert_equal(parse_app_white_list_json("{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
                                          "\"ios_mail_enable\":true,\"app\":{\"list\":["
                                          "{\"id\":\"com.tencent.xin\",\"name\":\"WeChat\"}],\"enable\":true}}"),
                1);
}

/* the notification the hook's 0x4B chunks were parsed into */
static const struct notif_msg *hook_notification(void) {
  return (const struct notif_msg *)&g1_ble()->tx_buf[4];
}

/* Pops the replies to the hook's 0x4B chunks, CB for each but the last, which gets C9; returns their number. */
static int expect_notification_replies(void) {
  int count = g1_frame_count();

  zassert_true(count >= 2, "%d reply frames", count);
  for (int index = 0; index < count; index++) {
    size_t len;
    const uint8_t *frame = g1_frame_peek(index, &len);
    uint8_t want = index == count - 1 ? 0xc9 : 0xcb;

    zassert_true(len >= 2 && frame[0] == 0x4b, "frame %d: %02x", index, frame[0]);
    zassert_equal(frame[1], want, "chunk %d of %d: status %02x, want %02x", index + 1, count, frame[1], want);
  }
  g1_frames_clear();
  return count;
}

ZTEST(proto_dispatch, test_0x23_1_title_message_goes_through_the_0x4b_path) {
  whitelist_hook_app();
  g1_send((const uint8_t *)"#1@title@msg", strlen("#1@title@msg"));

  expect_notification_replies();
  expect_str(hook_notification()->title, "title");
  expect_str(hook_notification()->message, "msg");
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

/* Leaves fill in the stack below the caller, where the uninitialised locals of its next call are. */
static __noinline void stack_fill(uint8_t fill) {
  volatile uint8_t area[8192];

  for (size_t index = 0; index < sizeof(area); index++) {
    area[index] = fill;
  }
}

/* #cmd@75@<title>#<message> sends the notification of "#1@<title>@<message>". */
ZTEST(proto_dispatch, test_0x23_cmd_75_title_ends_at_the_hash) {
  static const char text[] = "#cmd@75@Title#Message";

  whitelist_hook_app();
  stack_fill('~');
  g1_send((const uint8_t *)text, strlen(text));

  expect_notification_replies();
  expect_str(hook_notification()->title, "Title");
  expect_str(hook_notification()->message, "Message");
}

/* A '#' that is not in the text must not be taken for the separator. */
ZTEST(proto_dispatch, test_0x23_cmd_75_without_a_hash_is_an_unknown_put) {
  static const char text[] = "#cmd@75@no separator";

  stack_fill('#');
  g1_send((const uint8_t *)text, strlen(text));

  expect_only_frame_bytes(error_reply(0x23, "error put req."), 20);
}

/* "#1@" cuts its title to 31 characters: the same notification in as many chunks. */
ZTEST(proto_dispatch, test_0x23_cmd_75_title_longer_than_31_characters_is_cut) {
  char cut[32];
  char text[128];
  int chunks;
  int got;

  memset(cut, 't', sizeof(cut) - 1);
  cut[sizeof(cut) - 1] = '\0';
  whitelist_hook_app();
  snprintf(text, sizeof(text), "#1@%s@msg", cut);
  g1_send((const uint8_t *)text, strlen(text));
  chunks = expect_notification_replies();

  snprintf(text, sizeof(text), "#cmd@75@%s%s#msg", cut, cut);
  g1_send((const uint8_t *)text, strlen(text));

  got = expect_notification_replies();
  zassert_equal(got, chunks, "%d chunks, \"#1@\" sent %d", got, chunks);
  expect_str(hook_notification()->title, cut);
  expect_str(hook_notification()->message, "msg");
}

/* The hook keeps 127 characters of the text after "#cmd@75@": here "title#" and 121 of the 241 'm'. */
ZTEST(proto_dispatch, test_0x23_cmd_75_text_longer_than_127_characters_is_cut) {
  char text[255];
  char want[122];

  memset(text, 'm', sizeof(text));
  memcpy(text, "#cmd@75@title#", strlen("#cmd@75@title#"));
  memset(want, 'm', sizeof(want) - 1);
  want[sizeof(want) - 1] = '\0';
  whitelist_hook_app();

  g1_send((const uint8_t *)text, sizeof(text));

  expect_notification_replies();
  expect_str(hook_notification()->title, "title");
  expect_str(hook_notification()->message, want);
}

ZTEST(proto_dispatch, test_0x23_cmd_75_hash_past_the_127_kept_characters_is_an_unknown_put) {
  char text[255];

  memset(text, 't', sizeof(text));
  memcpy(text, "#cmd@75@", strlen("#cmd@75@"));
  text[strlen("#cmd@75@") + 127] = '#';

  g1_send((const uint8_t *)text, sizeof(text));

  expect_only_frame_bytes(error_reply(0x23, "error put req."), 20);
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

static struct {
  int init;
  int deinit;
  int flash;
  struct spi_master_ctx *deinit_ctx;
} spi_calls;

static int spi_init_stub(struct spi_master_ctx *ctx) {
  ARG_UNUSED(ctx);
  spi_calls.init++;
  return 0;
}

static int spi_deinit_stub(struct spi_master_ctx *ctx) {
  spi_calls.deinit++;
  spi_calls.deinit_ctx = ctx;
  return 0;
}

static void flash_hook_stub(struct flash_ops *ops) {
  ARG_UNUSED(ops);
  spi_calls.flash++;
}

ZTEST(proto_dispatch, test_0x23_j_does_not_call_an_unset_flash_callback) {
  /* set at boot by spi_master_register_context(), which this suite does not link */
  g1_state()->spi.init_cb = spi_init_stub;

  g1_send_hex("23 6a 31");
  expect_frame_padded("69 6e 69 74 20 66 6c 61 73 68", 20);
}

/* "#j0" shuts the SPI master down and leaves the flash hook alone; "#j1" runs init_cb and the hook. */
ZTEST(proto_dispatch, test_0x23_j_0_deinits_the_spi_master_and_j_1_inits_it) {
  static const uint8_t deinit_reply[20] = "deinit flash";
  static const uint8_t init_reply[20] = "init flash";

  memset(&spi_calls, 0, sizeof(spi_calls));
  g1_state()->spi.init_cb = spi_init_stub;
  g1_state()->spi.deinit_cb = spi_deinit_stub;
  g1_state()->flash_ops.fn = flash_hook_stub;

  g1_send_hex("23 6a 30");

  zassert_equal(spi_calls.deinit, 1, "deinit_cb called %d times", spi_calls.deinit);
  zassert_equal_ptr(spi_calls.deinit_ctx, &g1_state()->spi);
  zassert_equal(spi_calls.init, 0, "init_cb called %d times", spi_calls.init);
  zassert_equal(spi_calls.flash, 0, "flash hook called %d times", spi_calls.flash);
  expect_only_frame_bytes(deinit_reply, sizeof(deinit_reply));

  g1_send_hex("23 6a 31");

  zassert_equal(spi_calls.init, 1, "init_cb called %d times", spi_calls.init);
  zassert_equal(spi_calls.flash, 1, "flash hook called %d times", spi_calls.flash);
  zassert_equal(spi_calls.deinit, 1, "deinit_cb called %d times", spi_calls.deinit);
  expect_only_frame_bytes(init_reply, sizeof(init_reply));
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

static void send_text(const char *text) {
  g1_send((const uint8_t *)text, strlen(text));
}

/* "#i<n>": 0 and 1 switch auto brightness, 2 .. 42 set a manual level, anything else changes nothing. */
ZTEST(proto_dispatch, test_0x23_i_takes_only_brightness_levels) {
  static const uint8_t reply[20] = "base: 0";

  g1_state()->panel.panel_brightness_level = 0x15;
  g1_state()->opt3007_context.auto_brightness = 1;

  send_text("#i-1");
  zassert_equal(g1_state()->panel.panel_brightness_level, 0x15, "\"#i-1\" set level %d",
                g1_state()->panel.panel_brightness_level);
  zassert_equal(g1_state()->opt3007_context.auto_brightness, 1, "\"#i-1\" switched auto brightness off");
  expect_only_frame_bytes(reply, sizeof(reply));

  send_text("#i43");
  zassert_equal(g1_state()->panel.panel_brightness_level, 0x15, "\"#i43\" set level %d",
                g1_state()->panel.panel_brightness_level);
  zassert_equal(g1_state()->opt3007_context.auto_brightness, 1, "\"#i43\" switched auto brightness off");

  send_text("#i42");
  zassert_equal(g1_state()->panel.panel_brightness_level, 42);
  zassert_equal(g1_state()->opt3007_context.auto_brightness, 0);

  send_text("#i1");
  zassert_equal(g1_state()->opt3007_context.auto_brightness, 1);
  zassert_equal(g1_state()->panel.panel_brightness_level, 42, "\"#i1\" is not a level");

  send_text("#i0");
  zassert_equal(g1_state()->opt3007_context.auto_brightness, 0);
  zassert_equal(g1_state()->panel.panel_brightness_level, 42, "\"#i0\" is not a level");
}
