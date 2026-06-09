
add_subdirectory(bgl_base)


set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "bgl_base")

set_target_properties(bgl_base PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
