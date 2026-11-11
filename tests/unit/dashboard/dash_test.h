/*
 * Dashboard Set (0x06): 06 <len16> <seq> <sub> [<count16> <index16, 1-based>] <payload>.
 * Replies echo the header plus a status byte. ZTEST_SUITE(dashboard) is in test_time_calendar.c.
 */
#ifndef DASH_TEST_H_
#define DASH_TEST_H_

#include "fakes.h"
#include "ble/ble_proto.h"

#define DASH_SUB_CALENDAR  3
#define DASH_SUB_STOCK     4
#define DASH_SUB_NEWS      5
#define DASH_SUB_PANE_MODE 6
#define DASH_SUB_CANVAS    7

#define DASH_CHUNK_DATA 180

static inline struct dashboard_ts_context *dts(void)
{
  return g1_state()->dashboard_ts;
}

/* the 9-byte header of request, then status */
static inline void expect_chunk_reply(const uint8_t *request, uint8_t status)
{
  uint8_t reply[10];

  memcpy(reply, request, 9);
  reply[9] = status;
  expect_frame_bytes(reply, sizeof(reply));
}

static inline uint8_t *put_tlv8(uint8_t *pos, uint8_t type, const void *value, size_t len)
{
  *pos++ = type;
  *pos++ = (uint8_t)len;
  memcpy(pos, value, len);
  return pos + len;
}

static inline const char *repeat(char *buf, char fill, size_t len)
{
  memset(buf, fill, len);
  buf[len] = 0;
  return buf;
}

#endif /* DASH_TEST_H_ */
