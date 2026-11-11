# Builds one unit suite as a Zephyr app; tests/unit/CMakeLists.txt describes every suite.
#
# g1_unit_suite(<suite> [DRIVERS] [FIRMWARE <file under src/>...] [FAKES <name>... | all]
#               [KCONFIG <SYMBOL=value>...])
#   Describes <suite>; only the one named by -DG1_UNIT_SUITE (testcase.yaml) is built, from the
#   .c files in <suite>/ and the stand-in headers in <suite>/include/.
#   FIRMWARE: firmware sources, compiled unmodified.
#   FAKES: common/fakes_<name>.c (declared in common/fakes.h). Fakes are weak, so linking the real
#   source of a symbol overrides its fake; with --gc-sections a missing fake fails the link only
#   once a test reaches its caller.
#   DRIVERS: emulated devices (boards/native_sim_drivers.*, every emul/*.c, API in emul/drivers.h).
#   KCONFIG: Kconfig values without CONFIG_, applied after unit.conf and the DRIVERS conf.
# g1_unit_configure() runs before find_package(Zephyr), g1_unit_build() after it.

set(G1_UNIT_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(G1_UNIT_REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
set(G1_UNIT_FW_SRC_DIR ${G1_UNIT_REPO_DIR}/src)

# Phone protocol: dispatcher, request thread, notifications and the stores they write.
set(G1_FW_PROTOCOL ble/ble_proto.c threads/ble_req.c notify/notification.c
    notify/ncs_notification.c notify/whitelist.c utils/common_utils.c utils/md5.c
    utils/burial_point.c storage/settings_records.c storage/setting_store.c
    storage/quicknote_store.c storage/file_storage.c)

macro(g1_unit_suite suite)
  if("${suite}" STREQUAL "${G1_UNIT_SUITE}")
    cmake_parse_arguments(G1_SUITE "DRIVERS" "" "FIRMWARE;FAKES;KCONFIG" ${ARGN})
    set(G1_SUITE_FOUND ON)
  endif()
endmacro()

macro(g1_unit_configure)
  if(NOT G1_SUITE_FOUND)
    message(FATAL_ERROR "G1_UNIT_SUITE: no unit suite named '${G1_UNIT_SUITE}'")
  endif()
  set(G1_UNIT_SUITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${G1_UNIT_SUITE})
  set(CONF_FILE ${G1_UNIT_COMMON_DIR}/unit.conf)
  set(DTC_OVERLAY_FILE ${G1_UNIT_COMMON_DIR}/boards/native_sim.overlay)
  if(G1_SUITE_DRIVERS)
    list(APPEND CONF_FILE ${G1_UNIT_COMMON_DIR}/boards/native_sim_drivers.conf)
    list(APPEND DTC_OVERLAY_FILE ${G1_UNIT_COMMON_DIR}/boards/native_sim_drivers.overlay)
  endif()
  if(G1_SUITE_KCONFIG)
    list(TRANSFORM G1_SUITE_KCONFIG PREPEND "CONFIG_")
    list(JOIN G1_SUITE_KCONFIG "\n" G1_SUITE_KCONFIG)
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/suite.conf CONTENT "${G1_SUITE_KCONFIG}\n" @ONLY)
    list(APPEND CONF_FILE ${CMAKE_BINARY_DIR}/suite.conf)
  endif()
  list(APPEND DTS_ROOT ${G1_UNIT_COMMON_DIR})
endmacro()

macro(g1_unit_build)
  if("all" IN_LIST G1_SUITE_FAKES)
    file(GLOB G1_SUITE_FAKES ${G1_UNIT_COMMON_DIR}/fakes_*.c)
  else()
    list(TRANSFORM G1_SUITE_FAKES REPLACE "(.+)" "${G1_UNIT_COMMON_DIR}/fakes_\\1.c")
  endif()
  list(TRANSFORM G1_SUITE_FIRMWARE PREPEND ${G1_UNIT_FW_SRC_DIR}/)
  file(GLOB suite_sources CONFIGURE_DEPENDS ${G1_UNIT_SUITE_DIR}/*.c)
  target_sources(app PRIVATE ${suite_sources} ${G1_SUITE_FIRMWARE} ${G1_SUITE_FAKES}
                             ${G1_UNIT_COMMON_DIR}/harness.c ${G1_UNIT_COMMON_DIR}/frames.c)

  if(G1_SUITE_DRIVERS)
    file(GLOB driver_sources CONFIGURE_DEPENDS ${G1_UNIT_COMMON_DIR}/emul/*.c)
    target_sources(app PRIVATE ${driver_sources})
    target_include_directories(app PRIVATE ${G1_UNIT_COMMON_DIR}/emul)
  endif()
  if(EXISTS ${G1_UNIT_SUITE_DIR}/include)
    target_include_directories(app PRIVATE ${G1_UNIT_SUITE_DIR}/include)
  endif()
  # common/include: host stand-ins for nRF-only headers
  target_include_directories(app PRIVATE ${G1_UNIT_COMMON_DIR} ${G1_UNIT_COMMON_DIR}/include
                             ${G1_UNIT_FW_SRC_DIR}/include ${G1_UNIT_FW_SRC_DIR})
  # Plain char is unsigned on the lens (Arm) but signed on x86; build with the lens's signedness.
  target_compile_options(app PRIVATE -funsigned-char)
  # fixed, so replies that contain them are reproducible
  target_compile_definitions(app PRIVATE G1_BUILD_TIME="2024-01-01 00:00:00"
                                         G1_GIT_COMMIT="unittest")

  # app_version.h from the firmware's VERSION
  execute_process(
    COMMAND ${CMAKE_COMMAND} -DZEPHYR_BASE=${ZEPHYR_BASE}
            -DOUT_FILE=${CMAKE_BINARY_DIR}/app_version/app_version.h -DVERSION_TYPE=APP
            -DVERSION_FILE=${G1_UNIT_REPO_DIR}/VERSION -DAPP_BUILD_VERSION=unittest
            -P ${ZEPHYR_BASE}/cmake/gen_version_h.cmake
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${G1_UNIT_REPO_DIR}/VERSION)
  target_include_directories(app PRIVATE ${CMAKE_BINARY_DIR}/app_version)

  file(GLOB vectors CONFIGURE_DEPENDS ${G1_UNIT_REPO_DIR}/tests/unit/vectors/*.hex)
  g1_unit_embed(g1_vectors ${vectors})
endmacro()

# Embeds files as `const struct g1_unit_file <symbol>[]` (harness.h), ended by a NULL name.
# Each file's bytes are followed by a NUL, so text files can be read as strings.
function(g1_unit_embed symbol)
  set(body "#include \"harness.h\"\n\n")
  set(table "")
  set(index 0)
  foreach(path ${ARGN})
    get_filename_component(name ${path} NAME)
    file(READ ${path} hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR len "${hex_len} / 2")
    string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
    string(APPEND body "static const uint8_t ${symbol}_${index}[] = {${bytes}0};\n")
    string(APPEND table "\t{\"${name}\", ${symbol}_${index}, ${len}},\n")
    math(EXPR index "${index} + 1")
  endforeach()
  string(APPEND body "\nconst struct g1_unit_file ${symbol}[] = {\n${table}\t{NULL, NULL, 0},\n};\n")
  file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/${symbol}.c CONTENT "${body}" @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARGN})
  target_sources(app PRIVATE ${CMAKE_BINARY_DIR}/${symbol}.c)
endfunction()
