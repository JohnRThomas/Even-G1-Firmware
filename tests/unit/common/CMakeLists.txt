# g1_unit_common: harness, include paths and embedded test vectors for every suite.
# g1_unit_firmware(<group or path under src/>...) compiles firmware sources unmodified.
# g1_unit_fakes(<name>... | all) adds common/fakes_<name>.c. Fakes are weak, so linking the real
# source of a symbol overrides its fake; with --gc-sections a missing fake only fails the link once
# a test reaches its caller.
# G1_BUILD_TIME / G1_GIT_COMMIT are fixed so replies that contain them are reproducible.

set(G1_UNIT_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(G1_UNIT_REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
set(G1_UNIT_FW_SRC_DIR ${G1_UNIT_REPO_DIR}/src)

set(G1_UNIT_BUILD_TIME "2024-01-01 00:00:00")
set(G1_UNIT_GIT_COMMIT "unittest")

set(G1_FW_GROUP_ble_proto ble/ble_proto.c)
set(G1_FW_GROUP_ble_req threads/ble_req.c)
set(G1_FW_GROUP_notify notify/notification.c notify/ncs_notification.c notify/whitelist.c)
set(G1_FW_GROUP_common_utils utils/common_utils.c)
set(G1_FW_GROUP_md5 utils/md5.c)
set(G1_FW_GROUP_burial_point utils/burial_point.c)
set(G1_FW_GROUP_utils utils/common_utils.c utils/md5.c utils/burial_point.c)
set(G1_FW_GROUP_storage storage/settings_records.c storage/setting_store.c
    storage/quicknote_store.c storage/file_storage.c)
set(G1_FW_GROUP_protocol
    ${G1_FW_GROUP_ble_proto} ${G1_FW_GROUP_ble_req}
    ${G1_FW_GROUP_notify} ${G1_FW_GROUP_utils} ${G1_FW_GROUP_storage})

# Export to the suite's scope, where the functions below run.
foreach(var G1_UNIT_COMMON_DIR G1_UNIT_REPO_DIR G1_UNIT_FW_SRC_DIR G1_UNIT_BUILD_TIME
            G1_UNIT_GIT_COMMIT)
  set(${var} ${${var}} PARENT_SCOPE)
endforeach()
get_cmake_property(_g1_vars VARIABLES)
foreach(var ${_g1_vars})
  if(var MATCHES "^G1_FW_GROUP_")
    set(${var} ${${var}} PARENT_SCOPE)
  endif()
endforeach()

add_library(g1_unit_common INTERFACE)
target_include_directories(g1_unit_common INTERFACE
  ${G1_UNIT_COMMON_DIR}
  ${G1_UNIT_COMMON_DIR}/include    # host stand-ins for nRF-only headers
  ${G1_UNIT_FW_SRC_DIR}/include
  ${G1_UNIT_FW_SRC_DIR}
)
target_sources(g1_unit_common INTERFACE ${G1_UNIT_COMMON_DIR}/harness.c)
# Plain char is unsigned on the lens (Arm) but signed on x86; build with the lens's signedness.
target_compile_options(g1_unit_common INTERFACE -funsigned-char)
target_link_libraries(app PRIVATE g1_unit_common)

file(GLOB _g1_vectors CONFIGURE_DEPENDS ${G1_UNIT_REPO_DIR}/tests/cases/*.hex)
set(_g1_vectors_c ${CMAKE_CURRENT_BINARY_DIR}/g1_vectors.c)
add_custom_command(
  OUTPUT ${_g1_vectors_c}
  COMMAND ${CMAKE_COMMAND} -DOUT=${_g1_vectors_c} "-DFILES=${_g1_vectors}"
          -P ${G1_UNIT_COMMON_DIR}/gen_vectors.cmake
  DEPENDS ${_g1_vectors} ${G1_UNIT_COMMON_DIR}/gen_vectors.cmake
  COMMENT "Embedding tests/cases/*.hex"
  VERBATIM)
# carries the dependency into the suite's directory
add_custom_target(g1_unit_vectors DEPENDS ${_g1_vectors_c})
add_dependencies(app g1_unit_vectors)
set_source_files_properties(${_g1_vectors_c} TARGET_DIRECTORY app PROPERTIES GENERATED TRUE)
target_sources(app PRIVATE ${_g1_vectors_c})

function(g1_unit_firmware)
  get_property(added GLOBAL PROPERTY G1_UNIT_FW_FILES)
  foreach(item ${ARGN})
    if(DEFINED G1_FW_GROUP_${item})
      set(files ${G1_FW_GROUP_${item}})
    elseif(EXISTS ${G1_UNIT_FW_SRC_DIR}/${item} AND NOT IS_DIRECTORY ${G1_UNIT_FW_SRC_DIR}/${item})
      set(files ${item})
    else()
      message(FATAL_ERROR "g1_unit_firmware: '${item}' is neither a group (G1_FW_GROUP_*) "
                          "nor a file under ${G1_UNIT_FW_SRC_DIR}")
    endif()
    foreach(f ${files})
      if(f IN_LIST added)
        continue()
      endif()
      list(APPEND added ${f})
      set(source ${G1_UNIT_FW_SRC_DIR}/${f})
      if(f STREQUAL "ble/ble_proto.c")
        # compiled through ble_proto_reset.c, which includes it
        set(source ${G1_UNIT_COMMON_DIR}/ble_proto_reset.c)
        set_property(SOURCE ${source} TARGET_DIRECTORY app APPEND PROPERTY
                     COMPILE_DEFINITIONS G1_BUILD_TIME="${G1_UNIT_BUILD_TIME}"
                                         G1_GIT_COMMIT="${G1_UNIT_GIT_COMMIT}")
      endif()
      target_sources(app PRIVATE ${source})
    endforeach()
  endforeach()
  set_property(GLOBAL PROPERTY G1_UNIT_FW_FILES ${added})
endfunction()

function(g1_unit_fakes)
  set(names ${ARGN})
  if("all" IN_LIST names)
    file(GLOB all_fakes RELATIVE ${G1_UNIT_COMMON_DIR} ${G1_UNIT_COMMON_DIR}/fakes_*.c)
    set(names)
    foreach(f ${all_fakes})
      string(REGEX REPLACE "^fakes_(.*)\\.c$" "\\1" n ${f})
      list(APPEND names ${n})
    endforeach()
  endif()
  get_property(added GLOBAL PROPERTY G1_UNIT_FAKE_FILES)
  foreach(n ${names})
    set(f ${G1_UNIT_COMMON_DIR}/fakes_${n}.c)
    if(NOT EXISTS ${f})
      message(FATAL_ERROR "g1_unit_fakes: ${f} does not exist")
    endif()
    if(NOT n IN_LIST added)
      list(APPEND added ${n})
      target_sources(app PRIVATE ${f})
      if(n STREQUAL "nrfx")
        target_include_directories(app PRIVATE ${G1_UNIT_COMMON_DIR}/include_nrfx)
      endif()
    endif()
  endforeach()
  set_property(GLOBAL PROPERTY G1_UNIT_FAKE_FILES ${added})
endfunction()
