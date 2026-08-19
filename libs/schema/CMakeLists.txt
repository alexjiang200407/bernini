file(GLOB_RECURSE SCHEMA_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE SCHEMA_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

add_library(schema STATIC
	${SCHEMA_SOURCES}
	${SCHEMA_HEADERS}
)

target_include_directories(schema
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_precompile_headers(schema
	PUBLIC ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

target_link_libraries(schema PUBLIC core)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${SCHEMA_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/schema"
	PREFIX "Header Files" FILES ${SCHEMA_HEADERS})

enable_strict_compiler(schema)
enable_coverage(schema)

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE SCHEMA_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(schema_tests ${SCHEMA_TESTS_SOURCES})

	target_include_directories(schema_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
	)

	# The suite registers the real disk PODs, so it sees assetlib_structs; the library does not.
	target_link_libraries(schema_tests
		PRIVATE
			schema
			assetlib_structs
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(schema_tests
		PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(schema_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${SCHEMA_TESTS_SOURCES})

	enable_strict_compiler(schema_tests)
	enable_coverage(schema_tests)
endif()
