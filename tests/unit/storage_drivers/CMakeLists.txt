# ext_flash, flash_settings and file_storage on emulated flash
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_storage_drivers)

g1_unit_firmware(common_utils)
g1_unit_fakes(log misc display)
target_sources(app PRIVATE
  src/storage_fakes.c
  src/test_ext_flash.c
  src/test_flash_settings.c
  src/test_file_storage.c)
