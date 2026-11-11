# Encoding of the flash stores
cmake_minimum_required(VERSION 3.20.0)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_persistence)

# No file_storage.c: its storage_commit_request() would replace the fake
g1_unit_firmware(storage/settings_records.c storage/setting_store.c storage/quicknote_store.c)
g1_unit_fakes(all)
target_sources(app PRIVATE
  src/persistence.c
  src/test_sys_settings.c
  src/test_usr_settings.c
  src/test_burial_record.c
  src/test_factory_reset.c
  src/test_setting_store.c
  src/test_quicknote_store.c
)
