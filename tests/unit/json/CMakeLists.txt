cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_json)

g1_unit_firmware(notify/ncs_notification.c notify/whitelist.c common_utils)
g1_unit_fakes(log misc display storage)
target_sources(app PRIVATE src/test_ncs_notification.c src/test_whitelist.c)
