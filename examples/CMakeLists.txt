
add_subdirectory(bgl_base)
add_subdirectory(bgl_two_windows)

set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "bgl_base" "bgl_two_windows")

set_target_properties(bgl_base PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)


set_target_properties(bgl_two_windows PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)