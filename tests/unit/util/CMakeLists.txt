# Stateless src/utils helpers.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_util)

g1_unit_firmware(common_utils md5 storage/file_storage.c gui/gui_strings.c ui/ui_quicknote.c)
g1_unit_fakes(log misc)
target_sources(app PRIVATE src/test_util.c src/fake_language.c)
