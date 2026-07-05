# Shared, plain-data contracts produced by the asset loaders (assetlib) and consumed by
# other subsystems (bgl, editor). Kept free of heavy dependencies (no tinygltf /
# DirectXTex / meshoptimizer) so consumers can share the structs without pulling in the
# asset-parsing machinery.
#
# It is a normal STATIC library with a single stub translation unit (rather than an
# INTERFACE library) so the target and its headers show up in IDEs such as Visual Studio.
file(GLOB_RECURSE ASSETLIB_STRUCTS_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)

add_library(assetlib_structs STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/assetlib_structs.cpp
	${ASSETLIB_STRUCTS_HEADERS}
)

target_include_directories(assetlib_structs
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetlib_structs PUBLIC core)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/assetlib_structs"
	PREFIX "Header Files" FILES ${ASSETLIB_STRUCTS_HEADERS})
