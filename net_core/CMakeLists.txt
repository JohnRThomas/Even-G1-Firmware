# SPDX-License-Identifier: Apache-2.0
#
# Even Realities G1 network core firmware (reconstruction of v1.66).  Built as the
# network core child image of the application (../cmake/modules/mcuboot/CMakeLists.txt);
# NCS signs it for b0n and MCUboot into net_core_app_update.bin.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(net_core)

# Link order = target address order of the vendor code.
target_sources(app PRIVATE
  src/main.c
  src/ipc.c
  src/hci_rpmsg.c
  src/app_esb.c
  src/timeslot.c
)

# The NCS ESB driver signals its events on SWI0, which MPSL already uses on this core.
# src/esb_evt_irq.h moves them to SWI3; it is force-included in front of the SDK's esb.c
set(G1_ESB_DIR ${ZEPHYR_NRF_MODULE_DIR}/subsys/esb)
set(G1_ESB_LIB ..__nrf__subsys__esb)
if(NOT EXISTS ${G1_ESB_DIR}/esb.c OR NOT TARGET ${G1_ESB_LIB})
  message(FATAL_ERROR "NCS ESB driver (${G1_ESB_DIR}/esb.c, target ${G1_ESB_LIB}) not found: "
                      "cannot move its event interrupt off SWI0")
endif()
set_property(SOURCE ${G1_ESB_DIR}/esb.c TARGET_DIRECTORY ${G1_ESB_LIB} APPEND PROPERTY
             COMPILE_OPTIONS -I${G1_ESB_DIR} -include ${CMAKE_CURRENT_SOURCE_DIR}/src/esb_evt_irq.h)

# Build time reported to the application core: configure time.
string(TIMESTAMP G1_BUILD_TIME "%Y-%m-%d %H:%M:%S")
set_property(SOURCE src/main.c TARGET_DIRECTORY app APPEND PROPERTY
             COMPILE_DEFINITIONS G1_BUILD_TIME="${G1_BUILD_TIME}")
