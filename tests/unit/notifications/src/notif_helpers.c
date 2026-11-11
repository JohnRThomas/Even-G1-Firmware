#include <stdio.h>

#include "notif_test.h"

const char log_whitelist_json[] =
	"{\"calendar_enable\":true,\"call_enable\":true,\"msg_enable\":true,"
	"\"ios_mail_enable\":true,\"app\":{\"list\":["
	"{\"id\":\"nodomain.freeyourgadget.gadgetb\",\"name\":\"Gadgetbridge\"},"
	"{\"id\":\"com.discord\",\"name\":\"Discord\"},"
	"{\"id\":\"com.google.android.gm\",\"name\":\"Gmail\"}],\"enable\":true}}";

void set_whitelist(const char *json)
{
	zassert_equal(parse_app_white_list_json(json), 1, "whitelist rejected: %s", json);
}

const char *ncs_json(uint32_t msg_id, int action, const char *app, const char *title,
		     const char *message, const char *name)
{
	static char buf[1024];
	int n = snprintf(buf, sizeof(buf),
			 "{\"ncs_notification\":{\"msg_id\":%u,\"action\":%d,"
			 "\"app_identifier\":\"%s\",\"title\":\"%s\",\"subtitle\":\"\","
			 "\"message\":\"%s\",\"time_s\":1762899959,"
			 "\"date\":\"2025-11-11 14:25:59\",\"display_name\":\"%s\"}}",
			 msg_id, action, app, title, message, name);

	zassert_true(n > 0 && n < (int)sizeof(buf));
	return buf;
}

int send_ncs(const char *json, size_t chunk)
{
	size_t len = strlen(json);
	int total = (len + chunk - 1) / chunk;
	uint8_t pkt[256];

	zassert_true(chunk + 4 <= sizeof(pkt));
	for (int i = 0; i < total; i++) {
		size_t part = MIN(chunk, len - i * chunk);

		pkt[0] = 0x4b;
		pkt[1] = 0x00;
		pkt[2] = total;
		pkt[3] = i;
		memcpy(&pkt[4], json + i * chunk, part);
		g1_send(pkt, 4 + part);
	}
	return total;
}

void expect_4b_reply(uint8_t status)
{
	char hex[16];

	snprintf(hex, sizeof(hex), "4b %02x 00", status);
	expect_frame_padded(hex, 20);
}

void send_ncs_expect_ok(const char *json)
{
	int total = send_ncs(json, NCS_CHUNK_JSON);

	for (int i = 0; i < total; i++) {
		expect_4b_reply(i + 1 < total ? 0xcb : 0xc9);
	}
	expect_no_frame();
}

void make_msg(struct notif_msg *m, uint32_t uid, const char *app, const char *title)
{
	memset(m, 0, sizeof(*m));
	m->uid = uid;
	m->recv_time = get_date();
	strncpy(m->app_identifier, app, sizeof(m->app_identifier) - 1);
	strncpy(m->title, title, sizeof(m->title) - 1);
	strncpy(m->display_name, "Test", sizeof(m->display_name) - 1);
}

void put_slot(int i, uint32_t uid, const char *app, uint32_t recv, uint32_t show, int read)
{
	struct notif_msg *m = &g_arrMsgContent[i];

	memset(m, 0, sizeof(*m));
	m->uid = uid;
	m->recv_time = recv;
	m->start_show_time = show;
	m->read = read;
	m->pos = i;
	snprintf(m->title, sizeof(m->title), "msg %u", uid);
	strncpy(m->app_identifier, app, sizeof(m->app_identifier) - 1);
}

int slots_used(int from, int to)
{
	int n = 0;

	for (int i = from; i < to; i++) {
		n += g_arrMsgContent[i].app_identifier[0] != 0;
	}
	return n;
}
