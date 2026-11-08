target_sources(app PRIVATE
  display_thread.c
  input.c
  screen_dispatch.c
  screen_task.c
)

add_subdirectory(gui)
add_subdirectory(screens)
