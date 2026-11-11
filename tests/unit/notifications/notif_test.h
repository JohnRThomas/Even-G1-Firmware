/* Notification slots: 0..9 are shown messages, 10..19 the push queue. */
#ifndef NOTIF_TEST_H_
#define NOTIF_TEST_H_

#include "fakes.h"
#include "notify/notify.h"
#include "notify/whitelist.h"

extern uint8_t g_msg_num_push;
extern uint8_t g_curShowMsgIdx;

#define PUSH_FIRST MSG_NORMAL_NUM

static inline void make_msg(struct notif_msg *msg, uint32_t uid, const char *app,
          const char *title)
{
  memset(msg, 0, sizeof(*msg));
  msg->uid = uid;
  msg->recv_time = get_date();
  strncpy(msg->app_identifier, app, sizeof(msg->app_identifier) - 1);
  strncpy(msg->title, title, sizeof(msg->title) - 1);
  strncpy(msg->display_name, "Test", sizeof(msg->display_name) - 1);
}

static inline void put_slot(int slot, uint32_t uid, const char *app, uint32_t recv,
          uint32_t show, int read)
{
  struct notif_msg *msg = &g_arrMsgContent[slot];

  memset(msg, 0, sizeof(*msg));
  msg->uid = uid;
  msg->recv_time = recv;
  msg->start_show_time = show;
  msg->read = read;
  msg->pos = slot;
  snprintf(msg->title, sizeof(msg->title), "msg %u", uid);
  strncpy(msg->app_identifier, app, sizeof(msg->app_identifier) - 1);
}

static inline int slots_used(int from, int to)
{
  int count = 0;

  for (int slot = from; slot < to; slot++) {
    count += g_arrMsgContent[slot].app_identifier[0] != 0;
  }
  return count;
}

/* the slave has the queue head (sync state 4) and the phone confirms uid */
static inline void confirm_synced_head(uint32_t uid)
{
  set_msg_sync_state(PUSH_FIRST, 4);
  confirm_message(uid);
}

#endif /* NOTIF_TEST_H_ */
