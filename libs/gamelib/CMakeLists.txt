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

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE GAMELIB_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(gamelib_tests
		${GAMELIB_TESTS_SOURCES}
	)

	target_include_directories(gamelib_tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/include
			${CMAKE_CURRENT_SOURCE_DIR}/src
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
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

	enable_strict_compiler(gamelib_tests)
endif()
