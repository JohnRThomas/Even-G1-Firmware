/*
 * NCS (Android) notification JSON parser, called by
 * post_notification_cmd_process() (notification.c).
 */
#include <cJSON.h>
#include <errno.h>
#include <glasses_state.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "../utils/common_utils.h"
#include "g1_date.h"
#include "g1_log.h"
#include "notify.h"

/* cut a UTF-8 sequence left incomplete at the end of s */
static void utf8_drop_partial_tail(char *s) {
  size_t len = strlen(s);
  size_t lead = len;
  size_t need;

  while ((lead > 0) && (((uint8_t)s[lead - 1] & 0xc0U) == 0x80U)) {
    lead--;
  }
  if ((lead == 0) || (((uint8_t)s[lead - 1] & 0xc0U) != 0xc0U)) {
    return;
  }
  lead--;
  if (((uint8_t)s[lead] & 0xf0U) == 0xf0U) {
    need = 4;
  } else if (((uint8_t)s[lead] & 0xe0U) == 0xe0U) {
    need = 3;
  } else {
    need = 2;
  }
  if ((len - lead) < need) {
    s[lead] = 0;
  }
}

/* returns 0, or -EINVAL with msg untouched when json holds no ncs_notification object */
int parse_ncs_notification(const char *json, struct notif_msg *msg) {
  cJSON *root;
  const cJSON *ncs;
  const cJSON *app_identifier;
  const cJSON *title;
  const cJSON *subtitle;
  const cJSON *message;
  const cJSON *date;
  const cJSON *display_name;
  const cJSON *msg_id;
  const cJSON *action;

  root = cJSON_Parse(json);
  if (root == NULL) {
    G1_LOG_ERR("error root JSON NODE!\n");
    return -EINVAL;
  }
  ncs = cJSON_GetObjectItem(root, "ncs_notification");
  if (ncs == NULL) {
    G1_LOG_ERR("NOT FOUND NCS JSON NODE!\n");
    cJSON_Delete(root);
    return -EINVAL;
  }
  app_identifier = cJSON_GetObjectItem(ncs, "app_identifier");
  title = cJSON_GetObjectItem(ncs, "title");
  subtitle = cJSON_GetObjectItem(ncs, "subtitle");
  message = cJSON_GetObjectItem(ncs, "message");
  date = cJSON_GetObjectItem(ncs, "date");
  display_name = cJSON_GetObjectItem(ncs, "display_name");
  msg_id = cJSON_GetObjectItem(ncs, "msg_id");
  action = cJSON_GetObjectItem(ncs, "action");

  memset(msg, 0, sizeof(*msg));
  if (cJSON_IsString(app_identifier) != 0) {
    strncpy(msg->app_identifier, app_identifier->valuestring, 31);
    if (strcmp(app_identifier->valuestring, "com.android.phone_incall") == 0) {
      msg->start_show_time = get_date();
    }
  }
  if (cJSON_IsString(title) != 0) {
    strncpy(msg->title, title->valuestring, 31);
    utf8_drop_partial_tail(msg->title);
  }
  if (cJSON_IsString(message) != 0) {
    strncpy(msg->message, message->valuestring, 255);
  }
  if (cJSON_IsString(subtitle) != 0) {
    strncpy(msg->subtitle, subtitle->valuestring, 31);
  }
  if (cJSON_IsString(date) != 0) {
    strncpy(msg->date, date->valuestring, sizeof(msg->date) - 1U);
  }
  if (cJSON_IsString(display_name) != 0) {
    strncpy(msg->display_name, display_name->valuestring, 31);
  }
  if (msg_id != NULL) {
    /* u32 id (negative ones wrap); valueint saturates at INT_MAX */
    msg->uid = (uint32_t)(int64_t)msg_id->valuedouble;
  }
  if (action != NULL) {
    msg->action = action->valueint;
  }
  msg->recv_time = get_date();
  cJSON_Delete(root);
  dump_ancs("#device-receiver", msg);
  return 0;
}
