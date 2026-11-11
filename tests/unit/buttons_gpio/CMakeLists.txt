# button.c is #included by the test.
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_buttons_gpio)

g1_unit_emul(gpio_util)
g1_unit_fakes(log)
target_sources(app PRIVATE src/test_buttons.c)
