# Zephyr's dmic_nrfx_pdm.c runs on the nrfx fakes.
cmake_minimum_required(VERSION 3.20.0)
# interrupt controller binding for the overlay
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR})
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_audio)

g1_unit_firmware(ble_proto common_utils storage/setting_store.c storage/file_storage.c)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/pdm_driver.c
  src/fakes_ble_queue.c
  src/test_audio_stream.c
  src/test_dmic.c
)
target_include_directories(app PRIVATE src)
set_source_files_properties(src/pdm_driver.c PROPERTIES COMPILE_DEFINITIONS
  G1_UNIT_DMIC_NRFX_PDM_C="${ZEPHYR_BASE}/drivers/audio/dmic_nrfx_pdm.c")
