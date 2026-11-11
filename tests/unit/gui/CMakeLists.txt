# GUI drawing compared with golden images (see update_goldens.py).
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_gui)

g1_unit_firmware(gui/gui.c fonts/gui_fonts.c fonts/font_bitmap.c fonts/font_ascii.c
                 panel_display/panel_fb.c common_utils)
g1_unit_fakes(log misc storage display)
target_sources(app PRIVATE
  src/test_gui_text.c
  src/test_gui_draw.c
  src/test_fonts.c
  src/test_resources.c
  src/gui_fb.c
  src/fakes_gui.c)
target_include_directories(app PRIVATE src)

set(G1_GUI_ACTUAL_DIR ${CMAKE_BINARY_DIR}/gui_actual)
file(MAKE_DIRECTORY ${G1_GUI_ACTUAL_DIR})
target_compile_definitions(app PRIVATE G1_GUI_ACTUAL_DIR="${G1_GUI_ACTUAL_DIR}")

# runs in the native_sim runner (host libc)
target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/host_io.c)

file(GLOB _gui_goldens CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/golden/*.pbm
     ${CMAKE_CURRENT_SOURCE_DIR}/golden/*.pgm)
set(_gui_goldens_c ${CMAKE_CURRENT_BINARY_DIR}/gui_goldens.c)
add_custom_command(
  OUTPUT ${_gui_goldens_c}
  COMMAND ${CMAKE_COMMAND} -DOUT=${_gui_goldens_c} "-DFILES=${_gui_goldens}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_goldens.cmake
  DEPENDS ${_gui_goldens} ${CMAKE_CURRENT_SOURCE_DIR}/gen_goldens.cmake
  COMMENT "Embedding tests/unit/gui/golden/*"
  VERBATIM)
target_sources(app PRIVATE ${_gui_goldens_c})
