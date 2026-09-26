
add_subdirectory(util)
add_subdirectory(headless)

add_subdirectory(bgl_window)
add_subdirectory(bgl_base)
add_subdirectory(bgl_sphere)
add_subdirectory(bgl_moving)
add_subdirectory(bgl_blob_shadow)
add_subdirectory(bgl_directional_light)
add_subdirectory(bgl_grass)
add_subdirectory(bgl_two_windows)
add_subdirectory(bgl_gpu_assert)
add_subdirectory(bgl_ui)
add_subdirectory(bgl_pass_timings)
add_subdirectory(bgl_ai_viewer)

foreach(_ex bgl_base bgl_sphere bgl_moving bgl_blob_shadow bgl_directional_light bgl_two_windows bgl_gpu_assert bgl_ui bgl_pass_timings bgl_ai_viewer)
    set_target_properties(${_ex} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()
