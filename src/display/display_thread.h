/*
 * Display thread (UI framework dispatcher): private declarations.
 */
#ifndef G1_DISPLAY_THREAD_H_
#define G1_DISPLAY_THREAD_H_

#include <glasses_state.h>
#include <stddef.h>
#include <stdint.h>
#include <ui_event.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

struct GlassesState;
struct jbd_panel_context;

/* message posted to display_msgq */
enum disp_msg_type {
  DISP_MSG_OPEN = 2,         /* display_reflash() */
  DISP_MSG_CLOSE = 3,        /* display_close() */
  DISP_MSG_DELAY_CLOSE = 4,  /* display_DelayClose() */
  DISP_MSG_INPUT = 5,        /* display_inputEvent() */
  DISP_MSG_MASTER_CLOSE = 6, /* display_MasterSendClose() */
  DISP_MSG_LOW_POWER = 7,    /* display_powerEvent(1): the battery is low (UI_EVT_LOW_POWER) */
  DISP_MSG_POWER_RESUME = 8, /* display_powerEvent(!1): the battery is no longer low (UI_EVT_POWER_RESUME) */
};

struct disp_msg {
  enum disp_msg_type type;
  uint16_t len;             /* payload length */
  uint8_t data[20];         /* payload: [0] = 1 valid, [1] = screen id */
};

/* state of the display thread (struct disp_state.state) */
enum disp_thread_state {
  DISP_STATE_IDLE = 0,    /* no UI application: waits for DISP_MSG_OPEN */
  DISP_STATE_RUNNING = 1, /* a UI application is open and gets the messages and the refresh ticks */
  DISP_STATE_CLOSING = 2, /* DISP_MSG_CLOSE was delivered: the next pass goes back to idle */
} __packed;
BUILD_ASSERT(sizeof(enum disp_thread_state) == 1);

/* display thread state machine */
struct disp_state {
  enum disp_thread_state state;
  enum screen_id screen_id; /* the running UI application */
};

/* display_thread.c */
extern struct k_msgq display_msgq;
extern enum screen_id last_ui_screen_id; /* screen id of the last ui_task_handler() call */
int set_display_wait_ms(int ms);
int ui_task_handler(struct GlassesState *gs, enum screen_id screen_id, void *ctx, enum ui_event event);
int display_reflash_handler(struct GlassesState *gs, enum screen_id screen_id, void *ctx, enum ui_event event);
void display_timer_stop(void);
int get_display_delay_close_ms(void);
void display_thread_boost_priority(void);
void display_thread_restore_priority(void);
void display_thread_handler(struct GlassesState *gs);
int start_display_thread_handler(struct GlassesState *gs);
int display_reflash(const void *data, uint32_t len);
int display_close(const void *data, uint32_t len);
int display_close_screen(enum screen_id screen_id);
int display_DelayClose(int ms);
int display_inputEvent(uint8_t a, uint8_t b);
int display_MasterSendClose(void);
int display_powerEvent(int on);
int display_reflash_screen(enum screen_id screen_id);

#endif /* G1_DISPLAY_THREAD_H_ */
