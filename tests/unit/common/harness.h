/*
 * Unit-test harness on top of ztest.
 * Before every test g1_unit_reset() rebuilds GlassesState as init_context() leaves it, minus
 * hardware and flash (the SPI wiring stays zero: a suite that links spim.c sets it), runs the
 * fakes' reset hooks, resets the static state of the linked firmware files, clears the frame FIFO
 * and moves uptime to the next whole second; a suite's own before() runs after it. g1_unit_reboot()
 * does the same but keeps the flash and settings store, like a power cycle.
 * Not resettable: file_storage.c upload position, quicknote_cache and accumulate_elapsed_seconds()
 * slots (use restart = 1).
 * ble_context.tx_sem starts at 0; the 0x4D MTU handshake gives it. LOG_LEVEL is 2 after a reset;
 * set 4 for debug output.
 * The expect_*() macros fail the test at the caller's line.
 */
#ifndef G1_UNIT_HARNESS_H_
#define G1_UNIT_HARNESS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>

#include <glasses_state.h>
#include <g1_log.h>

void g1_unit_reset(void);
/* GlassesState keeps its address, so a pointer from g1_state() stays valid over the reboot */
void g1_unit_reboot(void);

typedef void (*g1_unit_reset_fn)(bool keep_media);
void g1_unit_register_reset(g1_unit_reset_fn hook);

#define G1_UNIT_RESET_HOOK(hook)                                                                   \
  static int hook##_register(void)                                                           \
  {                                                                                          \
    g1_unit_register_reset(hook);                                                      \
    return 0;                                                                          \
  }                                                                                          \
  SYS_INIT(hook##_register, APPLICATION, 0)

#define G1_ROLE_MASTER 1
#define G1_ROLE_SLAVE  2

struct GlassesState *g1_state(void);
struct ble_context *g1_ble(void);
void g1_unit_set_role(int role);
/* dashboard_position_sem was given: the request was handed to the dashboard thread */
bool g1_dashboard_notified(void);

/*
 * Runs entry(arg, NULL, NULL) on one of the harness threads (16 KiB stacks) from the caller's next
 * sleep; a thread still running after the test is aborted.
 */
#define G1_THREADS_MAX 4
k_tid_t g1_thread_start(k_thread_entry_t entry, void *arg, int prio);

/* the test's ble_printk() lines (ble_proto.c's ble_printk() sends them to the phone instead) */
int g1_printk_count(void);
const char *g1_printk_line(int index);

#define G1_FRAME_MAX  64
#define G1_FRAME_SIZE 1024

extern int g1_write_ret;

/* ble_context.write: queues the frame and returns g1_write_ret */
int g1_frame_capture(const uint8_t *data, uint16_t len);
int g1_frame_count(void);
/* returns the length, -ENOMSG when empty */
int g1_frame_pop(uint8_t *buf, size_t cap);
/* 0 = oldest; NULL if none; len may be NULL */
const uint8_t *g1_frame_peek(int index, size_t *len);
void g1_frames_clear(void);

/*
 * On a mismatch msg gets "differs at byte N", then the expected and the got bytes; buffers longer
 * than the 128-byte report window are shown from a little before byte N. A negative length (an
 * error code) or one above 16 MiB fails without reading the buffers.
 */
bool g1_bytes_check(const uint8_t *got, size_t got_len, const uint8_t *want, size_t want_len,
        char *msg, size_t msg_size);
/* got[0..len) is exactly the bytes of hex; a negative len or one above G1_FRAME_SIZE fails */
bool g1_hex_check(const void *got, size_t len, const char *hex, char *msg, size_t msg_size);
/* every byte of buf[0..len) is value; a negative len or one above 16 MiB fails */
bool g1_fill_check(const void *buf, size_t len, uint8_t value, const char *what, char *msg,
       size_t msg_size);
/* pops the oldest frame and compares it; pad_to >= 0 zero-pads hex to that length */
bool g1_frame_check(const char *hex, int pad_to, char *msg, size_t msg_size);
bool g1_frame_check_bytes(const uint8_t *want, size_t len, char *msg, size_t msg_size);
bool g1_frame_check_none(char *msg, size_t msg_size);

#define G1_MSG_SIZE 1024

/* statement macro around bool check(args..., msg, msg_size): a failure names check, then msg */
#define G1_EXPECT(check, ...)                                                                      \
  do {                                                                                       \
    static char _g1_msg[G1_MSG_SIZE];                                                  \
    zassert(check(__VA_ARGS__, _g1_msg, sizeof(_g1_msg)), #check " failed", "%s",      \
      _g1_msg);                                                                  \
  } while (0)

#define expect_frame(hex)                  G1_EXPECT(g1_frame_check, (hex), -1)
#define expect_frame_padded(hex, len)      G1_EXPECT(g1_frame_check, (hex), (len))
#define expect_frame_bytes(want, len)      G1_EXPECT(g1_frame_check_bytes, (want), (len))
#define expect_bytes(got, len, hex)        G1_EXPECT(g1_hex_check, (got), (len), (hex))
#define expect_fill(buf, len, value, what) G1_EXPECT(g1_fill_check, (buf), (len), (value), (what))

#define expect_no_frame()                                                                          \
  do {                                                                                       \
    static char _g1_msg[G1_MSG_SIZE];                                                  \
    zassert(g1_frame_check_none(_g1_msg, sizeof(_g1_msg)),                             \
      "g1_frame_check_none failed", "%s", _g1_msg);                              \
  } while (0)

/* the reply is the only frame sent */
#define expect_only_frame(hex)                                                                     \
  do {                                                                                       \
    expect_frame(hex);                                                                 \
    expect_no_frame();                                                                 \
  } while (0)

#define expect_only_frame_padded(hex, len)                                                         \
  do {                                                                                       \
    expect_frame_padded(hex, len);                                                     \
    expect_no_frame();                                                                 \
  } while (0)

/* expect_only_frame() with the frame given as bytes */
#define expect_only_frame_bytes(want, len)                                                         \
  do {                                                                                       \
    expect_frame_bytes(want, len);                                                     \
    expect_no_frame();                                                                 \
  } while (0)

/* expect_frame_padded() (pad_to -1: exact) for table rows: the message starts with the row label */
#define expect_frame_msg(hex, pad_to, fmt, ...)                                                    \
  do {                                                                                       \
    static char _g1_msg[G1_MSG_SIZE];                                                  \
    zassert(g1_frame_check((hex), (pad_to), _g1_msg, sizeof(_g1_msg)),                 \
      "g1_frame_check failed", fmt ": %s", ##__VA_ARGS__, _g1_msg);              \
  } while (0)

/* expect_bytes() for table rows and helpers: the message starts with the printf-style label */
#define expect_bytes_msg(got, len, hex, fmt, ...)                                                  \
  do {                                                                                       \
    static char _g1_msg[G1_MSG_SIZE];                                                  \
    zassert(g1_hex_check((got), (len), (hex), _g1_msg, sizeof(_g1_msg)),               \
      "g1_hex_check failed", fmt ": %s", ##__VA_ARGS__, _g1_msg);                \
  } while (0)

/* expect_no_frame() for table rows and helpers: the message starts with the printf-style label */
#define expect_no_frame_msg(fmt, ...)                                                              \
  do {                                                                                       \
    static char _g1_msg[G1_MSG_SIZE];                                                  \
    zassert(g1_frame_check_none(_g1_msg, sizeof(_g1_msg)),                             \
      "g1_frame_check_none failed", fmt ": %s", ##__VA_ARGS__, _g1_msg);         \
  } while (0)

/* Zephyr 3.4 has no zassert_str_equal(); a NULL got fails */
#define expect_str(got, want)                                                                      \
  do {                                                                                       \
    const char *_g1_got = (got);                                                       \
    const char *_g1_want = (want);                                                     \
    zassert_true(_g1_got != NULL && strcmp(_g1_got, _g1_want) == 0,                    \
           "%s is \"%s\", expected \"%s\"", #got, _g1_got ? _g1_got : "(null)",  \
           _g1_want);                                                            \
  } while (0)

/* zassert_within() that prints the value; expr is evaluated more than once */
#define expect_within(expr, want, tol)                                                             \
  zassert_within(expr, want, tol, "%s is %f", #expr, (double)(expr))

/* g1_clock_uptime_ms() - start_ms is within [min_ms, max_ms]; max_ms INT64_MAX: no upper bound */
#define expect_elapsed_ms(start_ms, min_ms, max_ms)                                                \
  do {                                                                                       \
    int64_t _g1_elapsed = g1_clock_uptime_ms() - (start_ms);                           \
    zassert_true(_g1_elapsed >= (min_ms) && _g1_elapsed <= (max_ms),                   \
           "%lld ms elapsed, expected %lld to %lld", (long long)_g1_elapsed,     \
           (long long)(min_ms), (long long)(max_ms));                            \
  } while (0)

/* the FFF fake returns value instead of running its custom fake */
#define G1_FAKE_RETURN(fake, value)                                                                \
  do {                                                                                       \
    fake##_fake.custom_fake = NULL;                                                    \
    fake##_fake.return_val = (value);                                                  \
  } while (0)

/* packet copies: FFF keeps only the pointer */
#define G1_PKT_LOG_MAX  32
#define G1_PKT_LOG_SIZE 512

struct g1_pkt_log {
  int count;
  int dropped;
  uint16_t len[G1_PKT_LOG_MAX]; /* length passed by the caller */
  uint8_t data[G1_PKT_LOG_MAX][G1_PKT_LOG_SIZE];
};

void g1_pkt_log_add(struct g1_pkt_log *log, const void *data, size_t len);
void g1_pkt_log_clear(struct g1_pkt_log *log);
/* entry index of the packet log is exactly the bytes of hex */
bool g1_pkt_check(const struct g1_pkt_log *log, int index, const char *hex, char *msg,
      size_t msg_size);
#define expect_pkt(log, index, hex) G1_EXPECT(g1_pkt_check, (log), (index), (hex))

void g1_send(const uint8_t *buf, size_t len);
void g1_send_hex(const char *hex);
void g1_send_vector(const char *name);
/* sends vector name with byte offset replaced by value; returns its length */
int g1_send_vector_with(const char *name, int offset, uint8_t value);

/* a file embedded by g1_unit_embed() (common/unit.cmake); data ends with an extra NUL */
struct g1_unit_file {
  const char *name;
  const uint8_t *data;
  size_t len;
};

int g1_hex_parse(const char *text, uint8_t *buf, size_t cap);
/* g1_hex_parse() that fails the test on bad hex; returns the length */
int g1_hex(const char *hex, uint8_t *buf, size_t cap);
/* "0x<id>_<tag>" from the tests/unit/vectors hex files: the length, -ENOENT if there is no such packet */
int g1_vector_load(const char *name, uint8_t *buf, size_t cap);
/* g1_vector_load() that fails the test unless the packet exists and is not empty */
int g1_vector(const char *name, uint8_t *buf, size_t cap);
/* NUL-terminated, cut with "..." if short */
void g1_hex_format(const uint8_t *buf, size_t len, char *out, size_t out_size);

#define G1_CHUNK_HDR_LEN 9

/*
 * the chunk header cmd, len16, seq, sub, total16, index16 (LE), then the payload (which may already
 * sit at pkt + G1_CHUNK_HDR_LEN); returns the packet length, which len16 holds
 */
size_t g1_put_chunk(uint8_t *pkt, uint8_t cmd, uint8_t seq, uint8_t sub, uint16_t total,
        uint16_t index, const void *payload, size_t payload_len);
/* buf[k] = (start + k) & 0xff */
void g1_fill_pattern(uint8_t *buf, size_t len, size_t start);
/* 8-bit additive checksum */
uint8_t g1_sum8(const uint8_t *buf, size_t len);

#define G1_RESET_DATE 1704067200u /* 2024-01-01 */

void g1_clock_advance_ms(uint32_t ms);
int64_t g1_clock_uptime_ms(void);
void g1_clock_set_date(uint32_t unix_s);

#endif /* G1_UNIT_HARNESS_H_ */
