# Feature-screen payload assembly.
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_features)

g1_unit_firmware(protocol)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/features.c
  src/test_text.c
  src/test_translate.c
  src/test_teleprompter.c
  src/test_quicknote.c
  src/test_navigation.c
  src/test_bitmap.c
  src/test_onboarding.c
  src/test_evenai.c
  src/test_countdown.c
  src/test_transcribe.c
  src/test_post49.c
)
