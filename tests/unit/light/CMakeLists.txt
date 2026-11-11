# opt3007.c is #included by the test.
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_light)

g1_unit_emul(opt3007_emul fake_npm1300)
g1_unit_fakes(log)
target_sources(app PRIVATE src/test_light.c)
