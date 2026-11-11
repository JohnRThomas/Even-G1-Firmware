#ifndef NOTIF_TEST_H_
#define NOTIF_TEST_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "ble/ble_proto.h"
#include "notify/notify.h"
#include "notify/whitelist.h"

extern uint8_t g_msg_num_push;
extern uint8_t g_curShowMsgIdx;
extern uint8_t msg_show_state;

#define PUSH_FIRST MSG_NORMAL_NUM

#define NCS_CHUNK_JSON 176

extern const char log_whitelist_json[];

#define assert_str(got, want)                                                                      \
	zassert_true(strcmp((got), (want)) == 0, "%s is \"%s\", expected \"%s\"", #got, (got),     \
		     (want))

void set_whitelist(const char *json);

const char *ncs_json(uint32_t msg_id, int action, const char *app, const char *title,
		     const char *message, const char *name);

int send_ncs(const char *json, size_t chunk);

void expect_4b_reply(uint8_t status);

/* Expects CB per chunk, C9 at the end. */
void send_ncs_expect_ok(const char *json);

void make_msg(struct notif_msg *m, uint32_t uid, const char *app, const char *title);

void put_slot(int i, uint32_t uid, const char *app, uint32_t recv, uint32_t show, int read);

int slots_used(int from, int to);

#endif /* NOTIF_TEST_H_ */
