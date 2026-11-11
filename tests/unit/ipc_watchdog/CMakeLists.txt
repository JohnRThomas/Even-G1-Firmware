cmake_minimum_required(VERSION 3.20.0)
set(G1_UNIT_DRIVERS ON)
include(${CMAKE_CURRENT_LIST_DIR}/../common/unit_setup.cmake)
project(unit_ipc_watchdog)

# ---- watchdog ----
g1_unit_firmware(threads/watchdog.c)
g1_unit_emul(fake_npm1300)
g1_unit_fakes(log)
target_sources(app PRIVATE src/test_watchdog.c)

# ---- IPC ----
g1_unit_fakes(ipc misc)
target_sources(app PRIVATE src/test_ipc_host.c)
