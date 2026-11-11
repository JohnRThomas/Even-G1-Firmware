# The unit suites, one Zephyr app each: twister builds this directory once per suite of
# testcase.yaml with -DG1_UNIT_SUITE=<suite>. common/unit.cmake documents g1_unit_suite().
cmake_minimum_required(VERSION 3.20.0)
include(common/unit.cmake)

g1_unit_suite(ahrs FIRMWARE ahrs/ahrs.c FAKES misc)
# liblc3 needs the FPU, Zephyr's dmic_nrfx_pdm.c onoff, the recording CRC check crc32_ieee()
g1_unit_suite(audio FIRMWARE ble/ble_proto.c utils/common_utils.c storage/setting_store.c
              storage/file_storage.c FAKES all KCONFIG CRC=y FPU=y LIBLC3=y ONOFF=y)
g1_unit_suite(burial FIRMWARE ${G1_FW_PROTOCOL} FAKES all)
g1_unit_suite(buttons_gpio DRIVERS FAKES misc)
g1_unit_suite(canvas_position FIRMWARE ui/ui_raster_height.c gui/gui.c fonts/gui_fonts.c
              fonts/font_bitmap.c fonts/font_ascii.c panel_display/panel_fb.c
              panel_display/jbd_panel_flush.c panel_display/jbd013_api.c
              panel_display/hal_driver.c utils/common_utils.c FAKES misc storage)
g1_unit_suite(dashboard FIRMWARE ${G1_FW_PROTOCOL} FAKES all)
g1_unit_suite(display_threads FIRMWARE utils/common_utils.c FAKES ipc storage misc)
g1_unit_suite(events FIRMWARE ${G1_FW_PROTOCOL} dashboard/dashboard.c FAKES all)
g1_unit_suite(features FIRMWARE ${G1_FW_PROTOCOL} FAKES all)
g1_unit_suite(gui FIRMWARE gui/gui.c fonts/gui_fonts.c fonts/font_bitmap.c fonts/font_ascii.c
              panel_display/panel_fb.c utils/common_utils.c FAKES misc storage display)
g1_unit_suite(harness FAKES all)
g1_unit_suite(imu DRIVERS FIRMWARE ahrs/ahrs.c FAKES misc esb ipc display)
# Zephyr's wdt_npm1300.c
g1_unit_suite(ipc_watchdog DRIVERS FIRMWARE threads/watchdog.c FAKES misc KCONFIG WATCHDOG=y)
g1_unit_suite(json FIRMWARE notify/ncs_notification.c notify/whitelist.c utils/common_utils.c
              FAKES misc display storage)
g1_unit_suite(light DRIVERS)
# nfc_wlc.c reads the field voltage on adc0 channel 3 and times in lens ticks
g1_unit_suite(nfc DRIVERS FIRMWARE nfc/eeprom_st25dv.c nfc/st25dv.c production_test/pt_comm.c
              sensors_audio/charger_box.c FAKES misc
              KCONFIG ADC=y ADC_EMUL=y SYS_CLOCK_TICKS_PER_SEC=32768)
g1_unit_suite(notifications FIRMWARE ${G1_FW_PROTOCOL} FAKES all)
# the 64 KB frame buffer and the harness state exceed the common arena
g1_unit_suite(panel FIRMWARE spim_spis/spim.c panel_display/jbd013_api.c
              panel_display/hal_driver.c panel_display/jbd_panel_flush.c panel_display/panel_fb.c
              KCONFIG COMMON_LIBC_MALLOC_ARENA_SIZE=245760)
# no file_storage.c: its storage_commit_request() would replace the fake
g1_unit_suite(persistence FIRMWARE storage/settings_records.c storage/setting_store.c
              storage/quicknote_store.c FAKES all)
g1_unit_suite(pkcs7 FIRMWARE ble/ble_proto.c utils/common_utils.c storage/setting_store.c
              FAKES all)
g1_unit_suite(power DRIVERS FIRMWARE sensors_audio/charger_box.c FAKES misc ipc display)
g1_unit_suite(production_test FIRMWARE production_test/pt_comm.c FAKES all)
g1_unit_suite(proto_dispatch FIRMWARE ${G1_FW_PROTOCOL} FAKES all)
g1_unit_suite(proto_session FIRMWARE ${G1_FW_PROTOCOL} FAKES all)
g1_unit_suite(screen_state FIRMWARE threads/task_status.c threads/system_state.c
              FAKES misc audio)
# settings on NVS, as the firmware
g1_unit_suite(storage_drivers DRIVERS FIRMWARE utils/common_utils.c FAKES misc display
              KCONFIG FLASH_MAP=y NVS=y SETTINGS=y SETTINGS_NVS=y)
# touch_key_thread times presses in lens ticks
g1_unit_suite(touch DRIVERS FAKES all KCONFIG SYS_CLOCK_TICKS_PER_SEC=32768)
g1_unit_suite(util FIRMWARE utils/common_utils.c utils/md5.c storage/file_storage.c
              gui/gui_strings.c ui/ui_quicknote.c FAKES misc)

g1_unit_configure()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_${G1_UNIT_SUITE})
g1_unit_build()

if(G1_UNIT_SUITE STREQUAL "gui")
  # renders are compared with gui/golden/; a mismatch goes to gui_actual/ for update_goldens.py
  file(GLOB goldens CONFIGURE_DEPENDS gui/golden/*.pbm gui/golden/*.pgm)
  g1_unit_embed(gui_goldens ${goldens})
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/gui_actual)
  target_compile_definitions(app PRIVATE G1_GUI_ACTUAL_DIR="${CMAKE_BINARY_DIR}/gui_actual")
  # host_io.c runs in the native_sim runner (host libc); every .c in gui/ goes into the app
  target_sources(native_simulator INTERFACE ${G1_UNIT_COMMON_DIR}/host_io.c)
elseif(G1_UNIT_SUITE STREQUAL "power")
  # NCS's nrf_fuel_gauge.h is plain C; power/ fakes the Arm-only library
  target_include_directories(app PRIVATE ${NRFXLIB_DIR}/nrf_fuel_gauge/include)
  # battery model text for .incbin
  set(battery_model ${G1_UNIT_FW_SRC_DIR}/sensors_audio/battery_model.inc)
  target_compile_definitions(app PRIVATE G1_BATTERY_MODEL_INC="${battery_model}")
  set_property(SOURCE power/test_fuel_gauge.c APPEND PROPERTY OBJECT_DEPENDS ${battery_model})
endif()
