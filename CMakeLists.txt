# SPDX-License-Identifier: Apache-2.0
#
# Even Realities G1 application core firmware (reconstruction of v1.66).
#
#   west build -b nrf5340dk_nrf5340_cpuapp   MCUboot, this image and the network core
#                                            image (net_core/), packaged in
#                                            build/zephyr/dfu_application.zip

cmake_minimum_required(VERSION 3.20.0)

# Module glue that adds net_core/ as the network core child image
# (cmake/modules/mcuboot/CMakeLists.txt).
list(APPEND MODULE_EXT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

set(REQIURED_NCS_VERSION 2.5.1)
set(_ncs_found "${NCS_VERSION_MAJOR}.${NCS_VERSION_MINOR}.${NCS_VERSION_PATCH}")
if(NOT _ncs_found VERSION_EQUAL REQIURED_NCS_VERSION)
  message(FATAL_ERROR "nRF Connect SDK ${REQIURED_NCS_VERSION} required, found ${_ncs_found} (${NRF_DIR})")
endif()

project(ar_glasses)

target_include_directories(app PRIVATE src/include)

# struct ncs_mem at 0x2007fc00 (shared with bootloader / net core)
zephyr_linker_sources(SECTIONS linker/ncs_mem.ld)

add_subdirectory(src)

# The NCS ANCS client accepts notification attributes of at most 32 bytes. This fix raises the limit
# to 256 bytes so larger messages can be set. It is force-included in front of the SDK source that
# checks it so the default value is overriden.
set(G1_ANCS_CLIENT_SRC ${ZEPHYR_NRF_MODULE_DIR}/subsys/bluetooth/services/ancs_client.c)
if(NOT EXISTS ${G1_ANCS_CLIENT_SRC})
  message(FATAL_ERROR "NCS ANCS client source not found: cannot raise its attribute limit")
endif()
set_property(SOURCE ${G1_ANCS_CLIENT_SRC} TARGET_DIRECTORY zephyr APPEND PROPERTY
             COMPILE_OPTIONS -include ${CMAKE_CURRENT_SOURCE_DIR}/src/ble/ancs_attr_max.h)

# The IMU has no power at boot, so Zephyr's LSM6DSO driver must not initialise it
# until it is powered on. Force-include the header in front of the driver to force
# the init later in the start up process..
set(G1_LSM6DSO_SRC ${ZEPHYR_BASE}/drivers/sensor/lsm6dso/lsm6dso.c)
set(G1_LSM6DSO_LIB drivers__sensor__lsm6dso)
if(NOT EXISTS ${G1_LSM6DSO_SRC} OR NOT TARGET ${G1_LSM6DSO_LIB})
  message(FATAL_ERROR "Zephyr LSM6DSO driver (${G1_LSM6DSO_SRC}, target ${G1_LSM6DSO_LIB}) "
                      "not found: cannot defer its initialisation")
endif()
set_property(SOURCE ${G1_LSM6DSO_SRC} TARGET_DIRECTORY ${G1_LSM6DSO_LIB} APPEND PROPERTY
             COMPILE_OPTIONS -include ${CMAKE_CURRENT_SOURCE_DIR}/src/imu/lsm6dso_power_on_init.h)


# Set BLE connection security to L2. 
zephyr_ld_options(-Wl,--wrap=bt_conn_add_le -Wl,--wrap=bt_conn_set_security)

# Build identification in the boot log and the "#t" debug command: configure time and
# the git commit of this tree.
string(TIMESTAMP G1_BUILD_TIME "%Y-%m-%d %H:%M:%S")
execute_process(COMMAND git rev-parse --short=8 HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE G1_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT G1_GIT_COMMIT)
  set(G1_GIT_COMMIT unknown)
endif()
set_property(SOURCE src/main.c src/ble/ble_proto.c TARGET_DIRECTORY app APPEND PROPERTY
             COMPILE_DEFINITIONS G1_BUILD_TIME="${G1_BUILD_TIME}" G1_GIT_COMMIT="${G1_GIT_COMMIT}")

# Once the DFU package is built, move it out of the build tree to
# ./dfu_application_<major>.<minor>.<patch>.zip, versioned from ./VERSION.
if(TARGET dfu_application_zip)
  set(G1_DFU_ZIP ${ZEPHYR_BINARY_DIR}/dfu_application.zip)
  set(G1_DFU_ZIP_OUT
      ${CMAKE_CURRENT_SOURCE_DIR}/dfu_application_${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_PATCHLEVEL}.zip)
  add_custom_target(dfu_application_versioned_zip ALL
    COMMAND ${CMAKE_COMMAND} -E copy ${G1_DFU_ZIP} ${G1_DFU_ZIP_OUT}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${G1_DFU_ZIP}
    COMMENT "Moving dfu_application.zip to ${G1_DFU_ZIP_OUT}"
    VERBATIM)
  add_dependencies(dfu_application_versioned_zip dfu_application_zip)
endif()
