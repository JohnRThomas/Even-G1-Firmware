/* Timestamps checked against Python's calendar.timegm(). */
#include "harness.h"
#include "storage/storage.h" /* crc32() */
#include "utils/common_utils.h"
#include "utils/md5.h"

/*
 * accumulate_elapsed_seconds() slots survive the harness reset, so tests restart theirs;
 * reference 0 means never started, so no test starts at uptime 0.
 */
static void util_before(void *fixture)
{
  ARG_UNUSED(fixture);
  g1_clock_advance_ms(1000);
}

ZTEST_SUITE(util, NULL, NULL, util_before, NULL, NULL);

/* reveng CRC catalogue: the check values are the CRCs of this input */
static const uint8_t crc_check[] = "123456789";

ZTEST(util, test_crc16_usb_matches_catalogue_check_value)
{
  zassert_equal(crc16_usb(0xffff, crc_check, 9), 0xb4c8, "got 0x%04x",
          crc16_usb(0xffff, crc_check, 9));
}

/* reflect(init) ^ xorout */
ZTEST(util, test_crc16_usb_of_empty_input_is_0)
{
  zassert_equal(crc16_usb(0xffff, crc_check, 0), 0x0000, "got 0x%04x",
          crc16_usb(0xffff, crc_check, 0));
}

ZTEST(util, test_crc16_generic_matches_catalogue_check_values)
{
  static const struct {
    const char *name;
    uint16_t poly, init, xorout;
    int refin;
    bool refout;
    uint16_t check;
  } models[] = {
    {"CRC-16/XMODEM", 0x1021, 0x0000, 0x0000, 0, false, 0x31c3},
    {"CRC-16/IBM-3740", 0x1021, 0xffff, 0x0000, 0, false, 0x29b1},
    {"CRC-16/GENIBUS", 0x1021, 0xffff, 0xffff, 0, false, 0xd64e},
    {"CRC-16/UMTS", 0x8005, 0x0000, 0x0000, 0, false, 0xfee8},
    {"CRC-16/ARC", 0x8005, 0x0000, 0x0000, 1, true, 0xbb3d},
    {"CRC-16/MODBUS", 0x8005, 0xffff, 0x0000, 1, true, 0x4b37},
    {"CRC-16/USB", 0x8005, 0xffff, 0xffff, 1, true, 0xb4c8},
    {"CRC-16/KERMIT", 0x1021, 0x0000, 0x0000, 1, true, 0x2189},
    {"CRC-16/IBM-SDLC", 0x1021, 0xffff, 0xffff, 1, true, 0x906e},
  };

  for (size_t row = 0; row < ARRAY_SIZE(models); row++) {
    uint16_t got = crc16_generic(models[row].init, models[row].poly, models[row].xorout,
               models[row].refin, models[row].refout, crc_check, 9);

    zassert_equal(got, models[row].check, "%s: got 0x%04x, catalogue check 0x%04x",
            models[row].name, got, models[row].check);
  }
}

/* CRC-32/XZ, catalogue check value */
ZTEST(util, test_crc32_matches_crc32_xz_check_value)
{
  zassert_equal(crc32(0, crc_check, 9), 0xcbf43926u, "got 0x%08x", crc32(0, crc_check, 9));
}

/* zlib.crc32() over address + data in one pass */
ZTEST(util, test_crc32_chained_over_bitmap_address_and_data_equals_one_pass_crc32_xz)
{
  static const uint8_t addr[4] = {0x00, 0x1c, 0x00, 0x00};
  static uint8_t data[0x1123];
  uint32_t crc;

  for (size_t offset = 0; offset < sizeof(data); offset++) {
    data[offset] = (uint8_t)(offset * 7 + 3);
  }

  crc = crc32(0, addr, 4);
  crc = crc32(crc, data, 0x1000);
  crc = crc32(crc, data + 0x1000, sizeof(data) - 0x1000);

  zassert_equal(crc, 0x1ee59b97u, "got 0x%08x", crc);
}

/* an end packet right after a full block */
ZTEST(util, test_crc32_of_zero_bytes_leaves_the_crc_unchanged)
{
  zassert_equal(crc32(0, crc_check, 0), 0u, "got 0x%08x", crc32(0, crc_check, 0));
  zassert_equal(crc32(0x1ee59b97u, crc_check, 0), 0x1ee59b97u, "got 0x%08x",
          crc32(0x1ee59b97u, crc_check, 0));
}

/* rle_decode() of src[0..len) into 0xee-filled dst returns decoded_len; dst starts with want */
static void expect_rle_decode(const uint8_t *src, uint32_t len, int decoded_len, const char *want)
{
  uint8_t want_bytes[16];
  int want_len = g1_hex(want, want_bytes, sizeof(want_bytes));
  uint8_t dst[16];
  int got_len;

  memset(dst, 0xee, sizeof(dst));
  got_len = rle_decode(src, len, dst);

  zassert_equal(got_len, decoded_len, "decoded length %d", got_len);
  expect_bytes(dst, want_len, want);
}

ZTEST(util, test_rle_decode_expands_count_value_pairs)
{
  static const uint8_t src[] = {0x03, 0xaa, 0x01, 0x55, 0x02, 0x00};

  expect_rle_decode(src, sizeof(src), 6, "aa aa aa 55 00 00 ee");
}

ZTEST(util, test_rle_decode_zero_count_pair_writes_nothing)
{
  static const uint8_t src[] = {0x00, 0xaa, 0x02, 0x55};

  expect_rle_decode(src, sizeof(src), 2, "55 55 ee");
}

ZTEST(util, test_rle_decode_zero_length_input_writes_nothing)
{
  static const uint8_t src[] = {0x04, 0xaa};

  expect_rle_decode(src, 0, 0, "ee ee ee ee ee ee ee ee");
}

/* DEFECT: an odd len reads src[len]; the fourth byte keeps the over-read in bounds. */
ZTEST_EXPECT_FAIL(util, test_rle_decode_ignores_trailing_count_without_value);
ZTEST(util, test_rle_decode_ignores_trailing_count_without_value)
{
  static const uint8_t src[] = {0x02, 0x11, 0x04, 0x77};

  expect_rle_decode(src, 3, 2, "11 11 ee ee ee ee ee");
}

static void expect_date(uint32_t ts, int year, int month, int day, int hour, int minute,
      int second)
{
  struct g1_date got;

  memset(&got, 0xff, sizeof(got));
  timestamp_to_date(ts, &got);

  zassert_true(got.year == year && got.month == month && got.day == day && got.hour == hour &&
           got.minute == minute && got.second == second,
         "%u: got %04u-%02u-%02u %02u:%02u:%02u, want %04d-%02d-%02d %02d:%02d:%02d",
         ts, got.year, got.month, got.day, got.hour, got.minute, got.second, year,
         month, day, hour, minute, second);
}

ZTEST(util, test_date_of_timestamp)
{
  expect_date(0, 1970, 1, 1, 0, 0, 0);
  expect_date(978307200u, 2001, 1, 1, 0, 0, 0); /* 2000 has 366 days */
  expect_date(1677628799u, 2023, 2, 28, 23, 59, 59);
  expect_date(1677628800u, 2023, 3, 1, 0, 0, 0);
  expect_date(1704067199u, 2023, 12, 31, 23, 59, 59);
  expect_date(1704067200u, 2024, 1, 1, 0, 0, 0);
  expect_date(1709210096u, 2024, 2, 29, 12, 34, 56);
  expect_date(1709251200u, 2024, 3, 1, 0, 0, 0);
  expect_date(2147483647u, 2038, 1, 19, 3, 14, 7); /* timestamps are uint32_t */
  expect_date(2147483648u, 2038, 1, 19, 3, 14, 8);
  expect_date(4107542399u, 2100, 2, 28, 23, 59, 59); /* 2100 is not a leap year */
  expect_date(4107542400u, 2100, 3, 1, 0, 0, 0);
  expect_date(0xffffffffu, 2106, 2, 7, 6, 28, 15);
}

/* DEFECT: IS_LEAP_YEAR is not parenthesised, so every month of 2000 has 29 days. */
ZTEST_EXPECT_FAIL(util, test_date_leap_day_2000_02_29);
ZTEST(util, test_date_leap_day_2000_02_29)
{
  expect_date(951782400u, 2000, 2, 29, 0, 0, 0);
}

/* DEFECT: same cause; got month 13, day 18. */
ZTEST_EXPECT_FAIL(util, test_date_last_day_of_2000);
ZTEST(util, test_date_last_day_of_2000)
{
  expect_date(978307199u, 2000, 12, 31, 23, 59, 59);
}

static void expect_weekday(uint32_t ts, const char *want)
{
  const char *got = get_weekday_name(ts);

  zassert_not_null(got, "%u: NULL", ts);
  zassert_true(strcmp(got, want) == 0, "%u: got \"%s\", want \"%s\"", ts, got, want);
}

ZTEST(util, test_weekday_name_of_timestamp)
{
  expect_weekday(0, "Thursday");
  expect_weekday(1704067199u, "Sunday"); /* the day changes at UTC midnight */
  expect_weekday(1704067200u, "Monday");
  expect_weekday(1709210096u, "Thursday"); /* leap day */
  expect_weekday(1709251200u, "Friday");
}

ZTEST(util, test_weekday_names_of_one_week_sunday_to_saturday)
{
  static const char *const names[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
  };

  for (int day = 0; day < 7; day++) {
    expect_weekday(1704585600u + day * 86400u + 43200u, names[day]);
  }
}

/* inputs are exact in binary */
static void expect_2dp(float val, const char *want)
{
  char buf[16];
  int len;

  memset(buf, '~', sizeof(buf));
  len = format_float_2dp(val, buf);

  zassert_true(len >= 0 && len < (int)sizeof(buf), "%s: returned length %d", want, len);
  zassert_true(len == (int)strlen(want) && memcmp(buf, want, len) == 0,
         "got \"%.*s\" (length %d), want \"%s\"", len, buf, len, want);
}

ZTEST(util, test_format_float_2dp_positive_values_get_plus_sign_and_two_decimals)
{
  expect_2dp(1.25f, "+1.25");
  expect_2dp(10.5f, "+10.50");
  expect_2dp(100.25f, "+100.25");
  expect_2dp(179.75f, "+179.75");
}

ZTEST(util, test_format_float_2dp_values_below_one_get_a_leading_zero)
{
  expect_2dp(0.5f, "+0.50");
  expect_2dp(0.25f, "+0.25");
  expect_2dp(0.09375f, "+0.09");
  expect_2dp(0.0625f, "+0.06");
}

ZTEST(util, test_format_float_2dp_negative_values_get_minus_sign)
{
  expect_2dp(-1.25f, "-1.25");
  expect_2dp(-0.25f, "-0.25");
  expect_2dp(-0.0625f, "-0.06");
  expect_2dp(-12.75f, "-12.75");
  expect_2dp(-179.5f, "-179.50");
}

/* DEFECT: (int)(val * 100) truncates instead of rounding (C11 %.2f). */
ZTEST_EXPECT_FAIL(util, test_format_float_2dp_rounds_to_nearest_hundredth);
ZTEST(util, test_format_float_2dp_rounds_to_nearest_hundredth)
{
  expect_2dp(1.2890625f, "+1.29");
  expect_2dp(-1.2890625f, "-1.29");
}

/* DEFECT: zero gets '-' and one digit: "-0.0". */
ZTEST_EXPECT_FAIL(util, test_format_float_2dp_zero_is_plus_0_00);
ZTEST(util, test_format_float_2dp_zero_is_plus_0_00)
{
  expect_2dp(0.0f, "+0.00");
}

ZTEST(util, test_uint32_to_little_endian_writes_4_bytes_lsb_first)
{
  uint8_t buf[6];
  int ret;

  memset(buf, 0xee, sizeof(buf));
  ret = uint32_to_little_endian(buf, 0x12345678u);

  zassert_equal(ret, 1, "returned %d", ret);
  expect_bytes(buf, 6, "78 56 34 12 ee ee");
}

ZTEST(util, test_uint32_to_little_endian_null_buffer_returns_0)
{
  zassert_equal(uint32_to_little_endian(NULL, 0x12345678u), 0);
}

ZTEST(util, test_elapsed_restart_adds_nothing)
{
  uint32_t total = 7;

  g1_clock_advance_ms(5000);
  accumulate_elapsed_seconds(&total, 1, 1);

  zassert_equal(total, 7, "total %u", total);
}

ZTEST(util, test_elapsed_adds_whole_seconds_since_restart)
{
  uint32_t total = 100;

  accumulate_elapsed_seconds(&total, 1, 1);
  g1_clock_advance_ms(2500);
  accumulate_elapsed_seconds(&total, 1, 0);

  zassert_equal(total, 102, "total %u", total);
}

ZTEST(util, test_elapsed_carries_the_sub_second_remainder)
{
  uint32_t total = 0;

  accumulate_elapsed_seconds(&total, 1, 1);
  g1_clock_advance_ms(2500);
  accumulate_elapsed_seconds(&total, 1, 0);
  zassert_equal(total, 2, "after 2500 ms: total %u", total);

  g1_clock_advance_ms(600);
  accumulate_elapsed_seconds(&total, 1, 0);
  zassert_equal(total, 3, "after 3100 ms: total %u", total);

  g1_clock_advance_ms(400);
  accumulate_elapsed_seconds(&total, 1, 0);
  zassert_equal(total, 3, "after 3500 ms: total %u", total);

  g1_clock_advance_ms(500);
  accumulate_elapsed_seconds(&total, 1, 0);
  zassert_equal(total, 4, "after 4000 ms: total %u", total);
}

ZTEST(util, test_elapsed_less_than_one_second_adds_nothing)
{
  uint32_t total = 0;

  accumulate_elapsed_seconds(&total, 2, 1);
  g1_clock_advance_ms(999);
  accumulate_elapsed_seconds(&total, 2, 0);

  zassert_equal(total, 0, "total %u", total);
}

ZTEST(util, test_elapsed_restart_discards_time_since_the_previous_reference)
{
  uint32_t total = 0;

  accumulate_elapsed_seconds(&total, 1, 1);
  g1_clock_advance_ms(5000);
  accumulate_elapsed_seconds(&total, 1, 1);
  g1_clock_advance_ms(1500);
  accumulate_elapsed_seconds(&total, 1, 0);

  zassert_equal(total, 1, "total %u", total);
}

ZTEST(util, test_elapsed_slots_are_independent)
{
  uint32_t total0 = 0;
  uint32_t total2 = 0;

  accumulate_elapsed_seconds(&total0, 0, 1);
  g1_clock_advance_ms(3000);
  accumulate_elapsed_seconds(&total2, 2, 1);
  g1_clock_advance_ms(1000);
  accumulate_elapsed_seconds(&total0, 0, 0);
  accumulate_elapsed_seconds(&total2, 2, 0);

  zassert_equal(total0, 4, "slot 0 total %u", total0);
  zassert_equal(total2, 1, "slot 2 total %u", total2);
}

/* slot 3 is used only here */
ZTEST(util, test_elapsed_first_call_on_a_never_used_slot_only_starts_counting)
{
  uint32_t total = 0;

  g1_clock_advance_ms(7000);
  accumulate_elapsed_seconds(&total, 3, 0);
  zassert_equal(total, 0, "first call: total %u", total);

  g1_clock_advance_ms(2000);
  accumulate_elapsed_seconds(&total, 3, 0);
  zassert_equal(total, 2, "second call: total %u", total);
}

/* RFC 1321 appendix A.5 */
static const struct {
  const char *msg;
  const char *digest;
} rfc1321_suite[] = {
  {"", "d4 1d 8c d9 8f 00 b2 04 e9 80 09 98 ec f8 42 7e"},
  {"a", "0c c1 75 b9 c0 f1 b6 a8 31 c3 99 e2 69 77 26 61"},
  {"abc", "90 01 50 98 3c d2 4f b0 d6 96 3f 7d 28 e1 7f 72"},
  {"message digest", "f9 6b 69 7d 7c b7 93 8d 52 5a 2f 31 aa f1 61 d0"},
  {"abcdefghijklmnopqrstuvwxyz", "c3 fc d3 d7 61 92 e4 00 7d fb 49 6c ca 67 e1 3b"},
  {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
   "d1 74 ab 98 d2 77 d9 f5 a5 61 1c 2c 9f 41 9d 9f"},
  {"1234567890123456789012345678901234567890123456789012345678901234567890"
   "1234567890",
   "57 ed f4 a2 2b e3 c9 55 ac 49 da 2e 21 07 b6 7a"},
};

ZTEST(util, test_md5_matches_rfc1321_vectors)
{
  for (size_t row = 0; row < ARRAY_SIZE(rfc1321_suite); row++) {
    const char *msg = rfc1321_suite[row].msg;
    uint8_t out[17];
    int ret;

    memset(out, 0xee, sizeof(out));
    ret = mbedtls_md5((const unsigned char *)msg, strlen(msg), out);

    zassert_equal(ret, 0, "MD5(\"%s\") returned %d", msg, ret);
    expect_bytes(out, 16, rfc1321_suite[row].digest);
    zassert_equal(out[16], 0xee, "MD5(\"%s\") wrote past 16 bytes", msg);
  }
}

/* the third piece completes the first 64-byte block */
ZTEST(util, test_md5_incremental_updates_across_a_block_boundary_give_the_rfc_digest)
{
  const unsigned char *msg = (const unsigned char *)rfc1321_suite[6].msg;
  mbedtls_md5_context ctx;
  uint8_t out[16];

  zassert_equal(strlen(rfc1321_suite[6].msg), 80);
  mbedtls_md5_starts(&ctx);
  mbedtls_md5_update(&ctx, msg, 1);
  mbedtls_md5_update(&ctx, msg + 1, 0);
  mbedtls_md5_update(&ctx, msg + 1, 62);
  mbedtls_md5_update(&ctx, msg + 63, 17);
  mbedtls_md5_finish(&ctx, out);

  expect_bytes(out, 16, rfc1321_suite[6].digest);
}
