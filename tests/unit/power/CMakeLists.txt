# nRF Fuel Gauge is faked: the library is Arm-only.
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_power)

g1_unit_firmware(sensors_audio/charger_box.c)
g1_unit_emul(fake_npm1300)
g1_unit_fakes(log)
target_include_directories(app PRIVATE include src)
target_sources(app PRIVATE
  src/fakes_power.c
  src/test_fuel_gauge.c
  src/test_battery_model.c
  src/test_charger_box.c
)
# model text for .incbin
set_property(SOURCE src/test_battery_model.c APPEND PROPERTY COMPILE_DEFINITIONS
             G1_BATTERY_MODEL_INC="${G1_UNIT_FW_SRC_DIR}/sensors_audio/battery_model.inc")
set_property(SOURCE src/test_battery_model.c APPEND PROPERTY OBJECT_DEPENDS
             ${G1_UNIT_FW_SRC_DIR}/sensors_audio/battery_model.inc)
