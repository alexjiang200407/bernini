

add_subdirectory(static_mesh_crud)
add_subdirectory(materials)
add_subdirectory(nvrhi_d3d12_example)


set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "static_mesh_crud")
set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "materials")
set_property(GLOBAL APPEND PROPERTY GLOBAL_EXAMPLE_TARGETS "nvrhi_d3d12_example")
