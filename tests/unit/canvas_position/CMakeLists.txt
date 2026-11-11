# Canvas placement for the height/depth settings (0x26 sub 2).
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_canvas_position)

g1_unit_firmware(ui/ui_raster_height.c gui/gui.c fonts/gui_fonts.c fonts/font_bitmap.c
                 fonts/font_ascii.c panel_display/panel_fb.c panel_display/jbd_panel_flush.c
                 panel_display/jbd013_api.c panel_display/hal_driver.c common_utils)
g1_unit_fakes(log misc storage display)
target_sources(app PRIVATE src/test_canvas_position.c)
