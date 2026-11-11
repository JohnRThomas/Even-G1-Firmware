# AW93203 driver and touch gestures on the chip emulator
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_touch)

g1_unit_emul(aw9320x_emul gpio_util)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/test_aw9320x.c
  src/test_gestures.c
  src/fakes_touch.c
)
target_include_directories(app PRIVATE include src)
