# Per-connection commands through the dispatcher.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_proto_session)

g1_unit_firmware(protocol)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/test_session.c
  src/test_settings.c
  src/test_hardware.c
  src/test_info.c
  src/test_control.c)

# app_version.h from VERSION; the tests derive the expected version from it
set(_g1_app_version_dir ${CMAKE_CURRENT_BINARY_DIR}/g1_app_version)
execute_process(
  COMMAND ${CMAKE_COMMAND} -DZEPHYR_BASE=${ZEPHYR_BASE}
          -DOUT_FILE=${_g1_app_version_dir}/app_version.h -DVERSION_TYPE=APP
          -DVERSION_FILE=${G1_UNIT_REPO_DIR}/VERSION -DAPP_BUILD_VERSION=unittest
          -P ${ZEPHYR_BASE}/cmake/gen_version_h.cmake
  RESULT_VARIABLE _g1_ret)
if(_g1_ret)
  message(FATAL_ERROR "app_version.h from ${G1_UNIT_REPO_DIR}/VERSION: ${_g1_ret}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${G1_UNIT_REPO_DIR}/VERSION)
target_include_directories(app PRIVATE ${_g1_app_version_dir})
