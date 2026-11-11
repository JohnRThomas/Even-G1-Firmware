# Include instead of find_package(Zephyr). testcase.yaml must not set extra_conf_files or
# extra_dtc_overlay_files: they would replace these lists.

set(CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/unit.conf)
if(G1_UNIT_DRIVERS)
  list(APPEND CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim_drivers.conf)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf)
  list(APPEND CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf)
endif()

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim.overlay)
if(G1_UNIT_DRIVERS)
  list(APPEND DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim_drivers.overlay)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/boards/native_sim.overlay)
  list(APPEND DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/boards/native_sim.overlay)
endif()

list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR} ${CMAKE_BINARY_DIR}/g1_unit_common)

if(G1_UNIT_DRIVERS)
  include(${CMAKE_CURRENT_LIST_DIR}/emul/emul.cmake)
endif()
