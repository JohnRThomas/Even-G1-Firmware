cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_ahrs)

g1_unit_firmware(ahrs/ahrs.c)
g1_unit_fakes(log misc)
target_sources(app PRIVATE src/test_ahrs.c)
