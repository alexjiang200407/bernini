
add_subdirectory(util)

# The new cross-backend "window + draw" example. Builds on any backend.
add_subdirectory(bgl_window)
set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "bgl_window")

# The rest are still Windows/D3D12-only (wWinMain, Shell32, the Agility SDK loader).
if(RENDERER_BACKEND STREQUAL "DX12")
    add_subdirectory(bgl_base)
    add_subdirectory(bgl_sphere)
    add_subdirectory(bgl_two_windows)
    add_subdirectory(bgl_gpu_assert)

    set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "bgl_base" "bgl_sphere" "bgl_two_windows" "bgl_gpu_assert")

    foreach(_ex bgl_base bgl_sphere bgl_two_windows bgl_gpu_assert)
        set_target_properties(${_ex} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endforeach()
endif()
