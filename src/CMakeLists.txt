target_sources(app PRIVATE
  main.c
)

add_subdirectory(comm)
add_subdirectory(drivers)
add_subdirectory(services)
add_subdirectory(system)
add_subdirectory(ui)
add_subdirectory(util)
