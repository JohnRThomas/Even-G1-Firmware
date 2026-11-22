/*
 * Events the display thread (display/display_thread.c) passes to the UI page
 * handlers, the ui_*_task() functions.
 */
#ifndef G1_UI_EVENT_H_
#define G1_UI_EVENT_H_

#include <zephyr/toolchain.h>

enum ui_event {
  UI_EVT_REFRESH = 0,      /* no message within the wait time: periodic redraw of the running page */
  UI_EVT_ENTER = 1,        /* display_reflash(): open the page, or new content for the running one */
  UI_EVT_EXIT = 2,         /* display_close() */
  UI_EVT_DELAY_CLOSE = 3,  /* display_DelayClose() */
  UI_EVT_INPUT = 4,        /* display_inputEvent() */
  UI_EVT_MASTER_CLOSE = 5, /* display_MasterSendClose() */
  UI_EVT_LOW_POWER = 6,    /* display_powerEvent(1): the battery is low */
  UI_EVT_POWER_RESUME = 7, /* display_powerEvent(!1): the battery is no longer low */
};

/*
 * State of a page handler that only tells "not drawn yet" from "on the screen" (the vendor's
 * DASHBOARD_INIT_STATUS / DASHBOARD_RUNING_STATUS): UI_EVT_ENTER draws the page and makes it
 * RUNNING, UI_EVT_EXIT clears the page's private data, which is INIT again.
 */
enum ui_page_state {
  UI_PAGE_INIT = 0,
  UI_PAGE_RUNNING = 1,
} __packed;
BUILD_ASSERT(sizeof(enum ui_page_state) == 1);

#endif /* G1_UI_EVENT_H_ */
