/*
 * Unit-test harness on top of ztest.
 * Before every test g1_unit_reset() rebuilds GlassesState as init_context() leaves it, runs the
 * fakes' reset hooks, resets the static state of the linked firmware files, clears the frame FIFO
 * and moves uptime to the next whole second; a suite's own before() runs after it. g1_unit_reboot()
 * does the same but keeps the flash and settings store, like a power cycle.
 * Not resettable: file_storage.c upload position, quicknote_cache and accumulate_elapsed_seconds()
 * slots (use restart = 1).
 * ble_context.tx_sem starts at 0; the 0x4D MTU handshake gives it. LOG_LEVEL is 2 after a reset;
 * set 4 for debug output.
 */
#ifndef G1_UNIT_HARNESS_H_
#define G1_UNIT_HARNESS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/ztest.h>
#include <zephyr/init.h>

#include <glasses_state.h>
#include <g1_log.h>

void g1_unit_reset(void);
void g1_unit_reboot(void);

typedef void (*g1_unit_reset_fn)(bool keep_media);
void g1_unit_register_reset(g1_unit_reset_fn fn);

#define G1_UNIT_RESET_HOOK(fn)                                                                     \
	static int fn##_register(void)                                                             \
	{                                                                                          \
		g1_unit_register_reset(fn);                                                        \
		return 0;                                                                          \
	}                                                                                          \
	SYS_INIT(fn##_register, APPLICATION, 0)

#define G1_ROLE_MASTER 1
#define G1_ROLE_SLAVE  2

struct GlassesState *g1_state(void);
struct ble_context *g1_ble(void);
void g1_unit_set_role(int role);

#define G1_FRAME_MAX  64
#define G1_FRAME_SIZE 1024

extern int g1_write_ret;

int g1_frame_count(void);
/* returns the length, -ENOMSG when empty */
int g1_frame_pop(uint8_t *buf, size_t cap);
/* 0 = oldest; NULL if none */
const uint8_t *g1_frame_peek(int i, size_t *len);
void g1_frames_clear(void);

bool g1_frame_check(const char *hex, int pad_to, char *msg, size_t msg_size);
bool g1_frame_check_none(char *msg, size_t msg_size);

#define G1_MSG_SIZE 1024

#define expect_frame(hex)                                                                          \
	do {                                                                                       \
		static char _g1_msg[G1_MSG_SIZE];                                                  \
		zassert_true(g1_frame_check((hex), -1, _g1_msg, sizeof(_g1_msg)), "%s", _g1_msg);  \
	} while (0)

#define expect_frame_padded(hex, len)                                                              \
	do {                                                                                       \
		static char _g1_msg[G1_MSG_SIZE];                                                  \
		zassert_true(g1_frame_check((hex), (len), _g1_msg, sizeof(_g1_msg)), "%s",         \
			     _g1_msg);                                                             \
	} while (0)

#define expect_no_frame()                                                                          \
	do {                                                                                       \
		static char _g1_msg[G1_MSG_SIZE];                                                  \
		zassert_true(g1_frame_check_none(_g1_msg, sizeof(_g1_msg)), "%s", _g1_msg);        \
	} while (0)

/* packet copies: FFF keeps only the pointer */
#define G1_PKT_LOG_MAX  32
#define G1_PKT_LOG_SIZE 512

struct g1_pkt_log {
	int count;
	int dropped;
	uint16_t len[G1_PKT_LOG_MAX];  /* length passed by the caller */
	uint8_t data[G1_PKT_LOG_MAX][G1_PKT_LOG_SIZE];
};

void g1_pkt_log_add(struct g1_pkt_log *log, const void *data, size_t len);
void g1_pkt_log_clear(struct g1_pkt_log *log);

void g1_send(const uint8_t *buf, size_t len);
void g1_send_hex(const char *hex);
void g1_send_vector(const char *name);

int g1_hex_parse(const char *text, uint8_t *buf, size_t cap);
int g1_vector_load(const char *name, uint8_t *buf, size_t cap);
/* NUL-terminated, cut with "..." if short */
void g1_hex_format(const uint8_t *buf, size_t len, char *out, size_t out_size);

#define G1_RESET_DATE 1704067200u /* 2024-01-01 */

void g1_clock_advance_ms(uint32_t ms);
int64_t g1_clock_uptime_ms(void);
void g1_clock_set_date(uint32_t unix_s);
void g1_clock_set_wall_ms(int64_t ms);

#endif /* G1_UNIT_HARNESS_H_ */
