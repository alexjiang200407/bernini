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
