/*
 * Template countdown timer page of the page dispatcher (ui_task_table in
 * ui_notification.c, screen id 2) and its helpers.
 */
#include <glasses_state.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>

#include "../fonts/font_bitmap.h"
#include "../panel_display/panel.h"
#include "../ui/ui.h"
#include "notify.h"

static uint32_t countdown_last_ms;

void countdown_tick(struct countdown_ts_context *cd) {
  uint32_t now;

  if (cd->reset == 1) {
    countdown_last_ms = 0;
    cd->reset = 0;
  }
  now = k_uptime_get_32();
  if (countdown_last_ms == 0U) {
    countdown_last_ms = now;
    return;
  }
  if ((now - countdown_last_ms) >= 1000U) {
    cd->seconds -= (now - countdown_last_ms) / 1000U;
    countdown_last_ms = now;
  }
}

/* One character of the remaining time: a 12 x 36 ASCII cell at byte column x. */
static void draw_countdown_char(uint8_t **fb, int code, int x) {
  copy_to_frame_buffer_raw(fb, find_chinese_bitmap_by_unicode(code, 12, 36), 12, 36, x, 12);
}

int draw_template_countdown_page(struct jbd_panel_context *panel, void *arg) {
  uint8_t **fb = panel->panel_buffer;
  struct GlassesState *state = get_dashboard_state();
  const struct ui_timer_payload *timer = &state->ui.timer;
  uint32_t hours = timer->seconds / 3600 % 24;
  uint32_t remain = timer->seconds % 3600;
  uint32_t secs = remain % 60U;
  bool done = (remain < 60U) && (hours == 0U) && (secs == 0U);
  uint16_t count = 0;
  const uint16_t *codes;
  int x;

  copy_to_frame_buffer_raw(fb, get_small_glyph_bitmap(done ? 4 : 9, 288), 12, 24, 12, 12);
  if (done) {
    codes = utf8_to_unicode_buf("时间到!", &count);
    for (int i = 0; i < count; i++) {
      copy_to_frame_buffer_raw(fb, find_chinese_bitmap_by_unicode(codes[i], 12, 36), 12, 36, (i * 12) + 32, 8);
    }
  } else {
    x = 12;
    if (hours != 0U) {
      uint32_t hours_tens = hours / 10U;
      uint32_t hours_ones = hours % 10U;

      draw_countdown_char(fb, '0' + (int)hours_tens, 32);
      draw_countdown_char(fb, '0' + (int)hours_ones, 47);
      draw_countdown_char(fb, ':', 62);
      x = 57;
    }
    uint32_t mins = remain / 60U;
    uint32_t mins_tens = mins / 10U;
    uint32_t mins_ones = mins % 10U;
    uint32_t secs_tens = secs / 10U;
    uint32_t secs_ones = secs % 10U;

    draw_countdown_char(fb, '0' + (int)mins_tens, x + 20);
    draw_countdown_char(fb, '0' + (int)mins_ones, x + 35);
    draw_countdown_char(fb, ':', x + 50);
    draw_countdown_char(fb, '0' + (int)secs_tens, x + 65);
    draw_countdown_char(fb, '0' + (int)secs_ones, x + 80);
  }
  if (timer->state >= 2) {
    copy_to_frame_buffer_raw(fb, get_small_glyph_bitmap(12, 288), 12, 24, 226, 20);
    codes = utf8_to_unicode_buf("结束中..", &count);
    x = 244;
    for (int i = 0; i < count; i++) {
      copy_to_frame_buffer_raw(fb, find_chinese_bitmap_by_unicode(codes[i], 12, 32), 12, 32, x, 8);
      x += 12;
    }
  } else {
    draw_countdown_running_status(fb, arg, 0, 0, 0);
  }
  return 0;
}

int draw_template_notification_page(struct jbd_panel_context *panel, void *arg) {
  ARG_UNUSED(panel);
  ARG_UNUSED(arg);
  return 0;
}

void countdown_reset(struct countdown_ts_context *cd) {
  cd->seconds = 60;
  cd->state = 0;
}
