# Dashboard Set (0x06) payload handling through the dispatcher.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_dashboard)

g1_unit_firmware(protocol)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/dash_helpers.c
  src/test_time_pane.c
  src/test_calendar.c
  src/test_stock_news.c
  src/test_canvas.c
)
target_include_directories(app PRIVATE src)
