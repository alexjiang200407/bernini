
add_subdirectory(util)

add_subdirectory(bgl_window)
add_subdirectory(bgl_base)
add_subdirectory(bgl_sphere)
add_subdirectory(bgl_moving)
add_subdirectory(bgl_two_windows)
add_subdirectory(bgl_gpu_assert)
add_subdirectory(bgl_ui)
add_subdirectory(bgl_pass_timings)

foreach(_ex bgl_base bgl_sphere bgl_moving bgl_two_windows bgl_gpu_assert bgl_ui bgl_pass_timings)
    set_target_properties(${_ex} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()
