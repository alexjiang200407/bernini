file(GLOB_RECURSE ASSETLIB_SCHEMA_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE ASSETLIB_SCHEMA_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

add_library(assetlib_schema STATIC
	${ASSETLIB_SCHEMA_SOURCES}
	${ASSETLIB_SCHEMA_HEADERS}
)

target_include_directories(assetlib_schema
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_precompile_headers(assetlib_schema
	PUBLIC ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

target_link_libraries(assetlib_schema PUBLIC core)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${ASSETLIB_SCHEMA_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/assetlib_schema"
	PREFIX "Header Files" FILES ${ASSETLIB_SCHEMA_HEADERS})

enable_strict_compiler(assetlib_schema)
enable_coverage(assetlib_schema)

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE ASSETLIB_SCHEMA_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(assetlib_schema_tests ${ASSETLIB_SCHEMA_TESTS_SOURCES})

	target_include_directories(assetlib_schema_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
	)

	# The suite registers the real disk PODs, so it sees assetlib_structs; the library does not.
	target_link_libraries(assetlib_schema_tests
		PRIVATE
			assetlib_schema
			assetlib_structs
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(assetlib_schema_tests
		PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(assetlib_schema_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${ASSETLIB_SCHEMA_TESTS_SOURCES})

	enable_strict_compiler(assetlib_schema_tests)
	enable_coverage(assetlib_schema_tests)
endif()
