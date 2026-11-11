# NFC tag, NDEF / WLC records and case link on the ST25DV emulator
cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_nfc)

g1_unit_firmware(nfc/eeprom_st25dv.c nfc/st25dv.c production_test/pt_comm.c
                 sensors_audio/charger_box.c)
g1_unit_emul(st25dv_emul fake_npm1300)
g1_unit_fakes(log misc)
target_include_directories(app PRIVATE src include)
target_sources(app PRIVATE
  src/fakes_nfc.c
  src/test_st25dv.c
  src/test_ndef.c
  src/test_wlc.c
  src/test_case_link.c
  src/test_case_events.c
)
