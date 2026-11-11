cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_pkcs7)

g1_unit_firmware(ble_proto common_utils storage/setting_store.c)
g1_unit_fakes(all)
target_sources(app PRIVATE src/test_pkcs7.c)
