# Usage counters of utils/burial_point.c.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_burial)

g1_unit_firmware(protocol)
g1_unit_fakes(all)
target_sources(app PRIVATE src/test_burial.c)
