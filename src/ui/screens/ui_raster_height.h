/*
 * Declarations of ui_raster_height.c.
 */
#ifndef G1_UI_SCREENS_UI_RASTER_HEIGHT_H_
#define G1_UI_SCREENS_UI_RASTER_HEIGHT_H_

#include "ui/screens/ui.h"

void cal_panel_canvas_coord(int *horizontal_offset, int *vertical_offset);
int ui_raster_height_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_RASTER_HEIGHT_H_ */
