# imu_fusion.c is #included by src/test_imu.c.
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_imu)

g1_unit_firmware(ahrs/ahrs.c)
g1_unit_fakes(log misc esb ipc display)
target_sources(app PRIVATE
  src/fakes_fusion.c
  src/test_imu.c
)
target_include_directories(app PRIVATE src)
