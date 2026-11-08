/*
 * Notification store / app white list / notification UI / dashboard info
 * - module private types and prototypes.
 */
#ifndef G1_NOTIFY_H_
#define G1_NOTIFY_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include "system/glasses_state.h"
#include <cJSON.h>

#include "system/logging.h"
#include "util/date.h"

#define MSG_NORMAL_NUM 10   /* g_arrMsgContent[0..9]: shown messages */
#define MSG_TOTAL_NUM  20   /* g_arrMsgContent[10..19]: push queue */

#define PKCS7_PACK_DONE 0xc9

/* GlassesState->schedule_notif */
struct schedule_notif {
	uint32_t ts;
	char title[17];
	char desc[0x97];
} __packed;

/* notification.c */
void dump_ancs(const char *tag, struct notif_msg *n);
extern struct notif_msg g_arrMsgContent[MSG_TOTAL_NUM];
extern uint8_t msg_array_busy;
void send_whilelist_app_info_wrapper(struct notif_msg *msg);
int post_notification_cmd_response(struct GlassesState *state, uint8_t *data, const uint8_t *src,
				   uint8_t *hdr, const uint8_t *arg, uint8_t *out, uint32_t len);
void post_notification_cmd_process(struct ble_context *ctx, const uint8_t *req, uint8_t *data);
int get_notification_counts_cmd_response(struct GlassesState *state, uint8_t *out);
void get_notification_counts_cmd_process(struct ble_context *ctx, uint8_t *buf, uint8_t *rsp);
struct notif_msg *get_push_message_copy(void);
struct notif_msg *get_push_message(void);
void set_msg_sync_state(int idx, uint8_t val);
uint8_t get_msg_sync_state(int idx);
struct notif_msg *find_push_message_by_uid(uint32_t uid);
struct notif_msg *get_message_by_index(int idx);
void clear_all_messages(void);
uint8_t count_messages(void);
uint8_t count_unread_messages(void);
void clear_timeout_message(int force);
void confirm_message(uint32_t uid);
uint8_t push_message(struct notif_msg *msg);
uint8_t g_arrMsgContent_count(void);
uint8_t pull_message(struct notif_msg **msg);
uint8_t get_cur_show_msg_idx(void);
void set_cur_message_state(void);
void mark_cur_message_read(void);
uint8_t get_msg_show_state(void);
void expire_cur_message(void);
void set_msg_show_state(uint8_t val);
uint8_t cur_message_show_state(void);
bool is_message_showing(void);
uint8_t parse_message_type(int idx);
void app_list_init(void);

/* ncs_notification.c */
void parse_ncs_notification(char *json, struct notif_msg *msg);

#endif /* G1_NOTIFY_H_ */
