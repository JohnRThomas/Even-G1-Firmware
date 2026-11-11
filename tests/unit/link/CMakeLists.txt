cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_link)

g1_unit_firmware(phone_silence)
g1_unit_fakes(bt log misc)
target_sources(app PRIVATE src/test_link.c)
