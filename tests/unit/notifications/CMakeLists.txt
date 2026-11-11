# Notification queue: 0x4B, 0x4C, 0x36, whitelist 0x04 / 0x2E
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_notifications)

g1_unit_firmware(protocol)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/notif_helpers.c
  src/test_0x4b.c
  src/test_queue.c
  src/test_whitelist_0x04.c
)
target_include_directories(app PRIVATE src)
