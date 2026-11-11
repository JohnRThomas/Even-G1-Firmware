# Display and proxy threads; the tests #include their sources
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_display_threads)

g1_unit_firmware(utils/common_utils.c)
g1_unit_fakes(ipc storage misc log)
target_sources(app PRIVATE
  src/fakes_ui.c
  src/test_display_thread.c
  src/test_proxy_thread.c
)
target_include_directories(app PRIVATE src)
