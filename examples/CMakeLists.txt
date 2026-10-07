
add_subdirectory(util)

# Vulkan's renderer is proven to present by the one example; the rest follow its other consumers.
if (NOT BERNINI_HAS_RENDERER_CONSUMERS)
    add_subdirectory(bgl_sphere)
    set_target_properties(bgl_sphere PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    return()
endif()

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
add_subdirectory(bgl_crowd)

foreach(_ex bgl_base bgl_sphere bgl_moving bgl_blob_shadow bgl_directional_light bgl_two_windows bgl_gpu_assert bgl_ui bgl_pass_timings bgl_crowd)
    set_target_properties(${_ex} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()
