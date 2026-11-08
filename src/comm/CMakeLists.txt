target_sources(app PRIVATE
  proxy_thread.c
)

add_subdirectory(ble)
add_subdirectory(esb)
add_subdirectory(ipc)
