# The terrain generator: a seed and a shape in, a heightfield out. Pure CPU, linking core and the
# heightfield's container alone, so the renderer, a cook and a game can all take what it makes
# without any of them linking the others. Static, like core: it holds no state of its own.
file(GLOB_RECURSE TERRAINLIB_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE TERRAINLIB_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/terrainlib/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files"
	FILES ${TERRAINLIB_SOURCES} ${TERRAINLIB_HEADERS})

add_library(terrainlib STATIC
	${TERRAINLIB_SOURCES}
	${TERRAINLIB_HEADERS}
)

target_include_directories(terrainlib
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(terrainlib PUBLIC assetlib_structs core)

target_precompile_headers(terrainlib PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

enable_strict_compiler(terrainlib)
enable_coverage(terrainlib)

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE TERRAINLIB_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(terrainlib_tests
		${TERRAINLIB_TESTS_SOURCES}
	)

	# The library's src/ too: the tests pin the noises the shapes are built from.
	target_include_directories(terrainlib_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
	)

	target_link_libraries(terrainlib_tests
		PRIVATE
			terrainlib
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(terrainlib_tests PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

	set_target_properties(terrainlib_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests" PREFIX "Source Files"
		FILES ${TERRAINLIB_TESTS_SOURCES})

	enable_strict_compiler(terrainlib_tests)
	enable_coverage(terrainlib_tests)
endif()
