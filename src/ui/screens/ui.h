/*
 * Shared definitions of the screens: the events a ui_*_task() handler receives,
 * language ids and the canvas origin.
 */
#ifndef G1_UI_H_
#define G1_UI_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "system/logging.h"
#include "main.h"
#include "ui/gui/gui.h"

/* events passed to the ui_*_task() handlers (display_thread.h) */
#define UI_EVT_REFRESH      0
#define UI_EVT_ENTER        1
#define UI_EVT_EXIT         2
#define UI_EVT_DELAY_CLOSE  3
#define UI_EVT_INPUT        4
#define UI_EVT_MASTER_CLOSE 5
#define UI_EVT_LOW_POWER    6
#define UI_EVT_POWER_RESUME 7

#define LANGUAGE_FRENCH  5
#define LANGUAGE_GERMAN  6
#define LANGUAGE_SPANISH 7
#define LANGUAGE_ITALIAN 14

/* canvas origin (depends on the panel position settings) */
#define X0 gui_screen_x_offset()
#define Y0 gui_screen_y_offset()

#endif /* G1_UI_H_ */
