# JBD013 panel: SPIM callbacks and panel drivers on the nrfx SPIM fakes.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_panel)

g1_unit_firmware(spim_spis/spim.c panel_display/jbd013_api.c panel_display/hal_driver.c
                 panel_display/jbd_panel_flush.c panel_display/panel_fb.c)
g1_unit_fakes(nrfx log)
target_sources(app PRIVATE
  src/panel_support.c
  src/test_spim.c
  src/test_panel_cmd.c
  src/test_panel_init.c
  src/test_panel_power.c
  src/test_panel_brightness.c
  src/test_panel_flush.c
  src/test_panel_fb.c
)
target_include_directories(app PRIVATE src)
