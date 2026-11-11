# The other PT sources are #included by the tests.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_production_test)

g1_unit_firmware(production_test/pt_comm.c)
g1_unit_fakes(all)
target_sources(app PRIVATE src/test_pt_data.c src/test_serial.c src/fakes_pt.c)
target_include_directories(app PRIVATE src)
