# Screen/task state machine. message_task.c is #included by test_screens.c (static handlers).
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_screen_state)

g1_unit_firmware(threads/task_status.c threads/system_state.c)
g1_unit_fakes(misc audio)
target_sources(app PRIVATE
  src/fakes_screen.c
  src/test_screens.c
  src/test_task_status.c
)
target_include_directories(app PRIVATE src)
