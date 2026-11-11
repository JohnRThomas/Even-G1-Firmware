# Driver substrate for G1_UNIT_DRIVERS suites; g1_unit_emul(<name>...) adds common/emul/<name>.c.

set(G1_UNIT_EMUL_DIR ${CMAKE_CURRENT_LIST_DIR})

target_include_directories(app PRIVATE ${G1_UNIT_EMUL_DIR})
target_sources(app PRIVATE ${G1_UNIT_EMUL_DIR}/devices.c ${G1_UNIT_EMUL_DIR}/nor_flash.c)
target_sources(app PRIVATE ${G1_UNIT_EMUL_DIR}/fake_imu.c)
target_sources(app PRIVATE ${G1_UNIT_EMUL_DIR}/fake_charger.c)

function(g1_unit_emul)
  get_property(added GLOBAL PROPERTY G1_UNIT_EMUL_FILES)
  foreach(n ${ARGN})
    set(f ${G1_UNIT_EMUL_DIR}/${n}.c)
    if(n STREQUAL "devices" OR n STREQUAL "nor_flash" OR n STREQUAL "fake_imu" OR
       n STREQUAL "fake_charger")
      continue() # always compiled
    endif()
    if(NOT EXISTS ${f})
      message(FATAL_ERROR "g1_unit_emul: ${f} does not exist")
    endif()
    if(NOT n IN_LIST added)
      list(APPEND added ${n})
      target_sources(app PRIVATE ${f})
    endif()
  endforeach()
  set_property(GLOBAL PROPERTY G1_UNIT_EMUL_FILES ${added})
endfunction()
