file(GLOB_RECURSE GAMELIB_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE GAMELIB_SHARED_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/gamelib/*.h
)

file(GLOB_RECURSE GAMELIB_INTERNAL_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${GAMELIB_SOURCES} ${GAMELIB_INTERNAL_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/gamelib" PREFIX "Shared Header Files" FILES ${GAMELIB_SHARED_HEADERS})

add_library(gamelib STATIC
	${GAMELIB_SOURCES}
	${GAMELIB_SHARED_HEADERS}
	${GAMELIB_INTERNAL_HEADERS}
)

target_include_directories(gamelib
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_precompile_headers(gamelib
	PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

target_link_libraries(gamelib
	PUBLIC
		core
		bgl
		assetlib
)

enable_strict_compiler(gamelib)
enable_coverage(gamelib)

# The suite drives a real scene through IGraphics, which needs a backend -- either backend:
# the DX12-only gate predated the Metal one being able to stand a device up.
if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE GAMELIB_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	# bgl's golden-image, environment and shader-cache helpers, borrowed rather than re-grown:
	# they speak only the public bgl API plus assetlib, both of which this target links. A list of
	# their own so source_group's TREE only sees files under this directory.
	set(GAMELIB_TESTS_BORROWED
		${CMAKE_SOURCE_DIR}/libs/bgl/tests/src/util/GoldenImage.cpp
		${CMAKE_SOURCE_DIR}/libs/bgl/tests/src/util/TestEnvironment.cpp
		${CMAKE_SOURCE_DIR}/libs/bgl/tests/src/util/TestOptions.cpp
	)

	add_executable(gamelib_tests
		${GAMELIB_TESTS_SOURCES}
		${GAMELIB_TESTS_BORROWED}
	)

	target_include_directories(gamelib_tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/include
			${CMAKE_CURRENT_SOURCE_DIR}/src
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
			${CMAKE_SOURCE_DIR}/libs/bgl/tests/src
	)

	target_force_include(gamelib_tests
		PRIVATE "${CMAKE_SOURCE_DIR}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
	)

	target_link_libraries(gamelib_tests
		PRIVATE
			gamelib
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(gamelib_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(gamelib_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${GAMELIB_TESTS_SOURCES})

	# The pixel test lights its view from "assets/Environments/forest.benv", resolved from the
	# runtime output directory like every bgl test asset; copy_assets is what stages it there.
	add_dependencies(gamelib_tests copy_assets)

	enable_strict_compiler(gamelib_tests)
	enable_coverage(gamelib_tests)
endif()
