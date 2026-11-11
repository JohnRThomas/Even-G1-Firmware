/*
 * 0x22 status. Head up: 22 0A 00 00 01 <unread> <low power> <mode> <pane> <page>;
 * tap: 22 08 00 00 02 <mode> <pane> <page>.
 */
#include <string.h>
#include <stdio.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_esb.h"
#include "events_fakes.h"
#include "dashboard/dashboard.h"
#include "notify/notify.h"

enum { MODE_FULL = 0, MODE_DUAL = 1, MODE_MINIMAL = 2 };
enum { PANE_NOTES = 0, PANE_STOCK = 1, PANE_NEWS = 2, PANE_CALENDAR = 3, PANE_MAP = 4,
       PANE_EMPTY = 5 };

static void dashboard(uint8_t mode, uint8_t pane)
{
	g1_state()->dashboard_ts->disp_mode = mode;
	g1_state()->dashboard_ts->area = pane;
	_dashboard_prv_data.pane_mode = pane;
}

static void pages(uint8_t notes, uint8_t stocks, uint8_t news, uint8_t calendar)
{
	notes_index = notes;
	stocks_index = stocks;
	news_index = news;
	calendar_index = calendar;
}

static void messages(int n, int read)
{
	memset(g_arrMsgContent, 0, sizeof(g_arrMsgContent));
	for (int i = 0; i < n; i++) {
		strcpy(g_arrMsgContent[i].app_identifier, "com.example.chat");
		g_arrMsgContent[i].uid = 100 + i;
		g_arrMsgContent[i].read = i < read;
	}
}

static void head_up(void)
{
	zassert_equal(send_message_count_to_app(), 0);
}

static void tap(void)
{
	zassert_equal(sync_dashboard_key_event_to_app(), 0);
}

/* ---- head up: 22 0A ---- */

ZTEST(events, test_head_up_status_is_22_0a_with_unread_low_power_mode_pane_and_page)
{
	dashboard(MODE_DUAL, PANE_CALENDAR);
	messages(5, 0);

	head_up();

	expect_frame("22 0a 00 00 01 05 00 01 03 01");
	expect_no_frame();
	zassert_equal(g1_events_response_send_count, 1);
}

ZTEST(events, test_head_up_unread_count_leaves_out_read_messages)
{
	dashboard(MODE_DUAL, PANE_MAP);
	messages(6, 2);

	head_up();

	expect_frame("22 0a 00 00 01 04 00 01 04 00");
}

ZTEST(events, test_head_up_reports_low_power)
{
	dashboard(MODE_DUAL, PANE_MAP);
	g1_state()->dashboard_ts->low_power = 1;

	head_up();

	expect_frame("22 0a 00 00 01 00 01 01 04 00");
}

ZTEST(events, test_head_up_notes_pane_reports_the_note_page)
{
	dashboard(MODE_FULL, PANE_NOTES);

	head_up();
	expect_frame("22 0a 00 00 01 00 00 00 00 01");

	pages(2, 0, 0, 0);
	head_up();
	expect_frame("22 0a 00 00 01 00 00 00 00 03");
}

ZTEST(events, test_head_up_stock_pane_reports_the_stock_page)
{
	dashboard(MODE_DUAL, PANE_STOCK);

	head_up();
	expect_frame("22 0a 00 00 01 00 00 01 01 01");

	pages(0, 3, 0, 0);
	head_up();
	expect_frame("22 0a 00 00 01 00 00 01 01 04");
}

ZTEST(events, test_head_up_news_pane_reports_the_news_page)
{
	dashboard(MODE_DUAL, PANE_NEWS);

	head_up();
	expect_frame("22 0a 00 00 01 00 00 01 02 01");

	pages(0, 0, 1, 0);
	head_up();
	expect_frame("22 0a 00 00 01 00 00 01 02 02");
}

ZTEST(events, test_head_up_calendar_pane_reports_the_calendar_page)
{
	dashboard(MODE_DUAL, PANE_CALENDAR);
	pages(0, 0, 0, 1);

	head_up();

	expect_frame("22 0a 00 00 01 00 00 01 03 02");
}

ZTEST(events, test_head_up_map_pane_has_page_0)
{
	dashboard(MODE_DUAL, PANE_MAP);
	pages(1, 1, 1, 1);

	head_up();

	expect_frame("22 0a 00 00 01 00 00 01 04 00");
}

ZTEST(events, test_head_up_empty_pane_in_minimal_mode_has_page_0)
{
	dashboard(MODE_MINIMAL, PANE_EMPTY);
	pages(1, 1, 1, 1);
	messages(4, 0);

	head_up();

	expect_frame("22 0a 00 00 01 04 00 02 05 00");
}

ZTEST(events, test_head_up_reports_each_dashboard_mode)
{
	static const uint8_t modes[] = {MODE_FULL, MODE_DUAL, MODE_MINIMAL};
	char hex[40];

	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		dashboard(modes[i], PANE_NOTES);
		head_up();
		snprintf(hex, sizeof(hex), "22 0a 00 00 01 00 00 %02x 00 01", modes[i]);
		expect_frame(hex);
	}
}

ZTEST(events, test_head_up_status_is_not_sent_by_the_slave)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	dashboard(MODE_DUAL, PANE_CALENDAR);

	head_up();

	expect_no_frame();
	zassert_equal(g1_esb_response_log.count, 0);
}

/* ---- tap: 22 08 ---- */

ZTEST(events, test_tap_status_is_22_08_with_mode_pane_and_page)
{
	dashboard(MODE_DUAL, PANE_CALENDAR);
	messages(5, 0);

	tap();

	expect_frame("22 08 00 00 02 01 03 01");
	expect_no_frame();
	zassert_equal(g1_events_response_send_count, 1);
}

ZTEST(events, test_tap_calendar_pane_reports_the_calendar_page)
{
	dashboard(MODE_DUAL, PANE_CALENDAR);

	for (uint8_t idx = 0; idx < 3; idx++) {
		char hex[32];

		pages(0, 0, 0, idx);
		tap();
		snprintf(hex, sizeof(hex), "22 08 00 00 02 01 03 %02x", idx + 1);
		expect_frame(hex);
	}
}

ZTEST(events, test_tap_notes_pane_reports_the_note_page)
{
	dashboard(MODE_FULL, PANE_NOTES);

	tap();
	expect_frame("22 08 00 00 02 00 00 01");

	pages(3, 0, 0, 0);
	tap();
	expect_frame("22 08 00 00 02 00 00 04");
}

ZTEST(events, test_tap_stock_pane_reports_the_stock_page)
{
	dashboard(MODE_FULL, PANE_STOCK);
	pages(0, 1, 0, 0);

	tap();

	expect_frame("22 08 00 00 02 00 01 02");
}

ZTEST(events, test_tap_news_pane_reports_the_news_page)
{
	dashboard(MODE_DUAL, PANE_NEWS);
	pages(0, 0, 2, 0);

	tap();

	expect_frame("22 08 00 00 02 01 02 03");
}

ZTEST(events, test_tap_map_and_empty_panes_have_page_0)
{
	pages(1, 1, 1, 1);

	dashboard(MODE_DUAL, PANE_MAP);
	tap();
	expect_frame("22 08 00 00 02 01 04 00");

	dashboard(MODE_MINIMAL, PANE_EMPTY);
	tap();
	expect_frame("22 08 00 00 02 02 05 00");
}

ZTEST(events, test_tap_status_is_not_sent_by_the_slave)
{
	g1_unit_set_role(G1_ROLE_SLAVE);
	dashboard(MODE_DUAL, PANE_CALENDAR);

	tap();

	expect_no_frame();
	zassert_equal(g1_esb_response_log.count, 0);
}
