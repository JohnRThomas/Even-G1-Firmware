# Glasses -> phone events: 0xF5, 0x22, 0xF4, 0xF1.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_events)

g1_unit_firmware(protocol dashboard/dashboard.c)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/events_fakes.c
  src/test_f5_events.c
  src/test_status_0x22.c
  src/test_debug_channel.c
  src/test_audio_f1.c
)
