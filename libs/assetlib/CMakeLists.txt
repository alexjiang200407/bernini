file(GLOB_RECURSE ASSETLIB_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE ASSETLIB_SHARED_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/assetlib/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/assetlib/*.hpp
)

file(GLOB_RECURSE ASSETLIB_INTERNAL_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${ASSETLIB_SOURCES} ${ASSETLIB_INTERNAL_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/assetlib" PREFIX "Shared Header Files" FILES ${ASSETLIB_SHARED_HEADERS})

add_library(assetlib STATIC
	${ASSETLIB_SOURCES}
	${ASSETLIB_SHARED_HEADERS}
	${ASSETLIB_INTERNAL_HEADERS}
)

target_include_directories(assetlib
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_precompile_headers(assetlib
	PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

find_package(nlohmann_json CONFIG REQUIRED)     # required by tinygltf
find_package(Stb REQUIRED)                      # image decoding used by tinygltf
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")  # header-only glTF 2.0 loader
find_package(meshoptimizer CONFIG REQUIRED)     # vertex optimization + meshlet generation

if (WIN32)
	find_package(directxtex CONFIG REQUIRED)    # glTF textures -> in-memory DDS
endif()

target_link_libraries(assetlib
	PUBLIC
		core
		assetlib_structs
	PRIVATE
		nlohmann_json::nlohmann_json
		meshoptimizer::meshoptimizer
		$<$<PLATFORM_ID:Windows>:Microsoft::DirectXTex>
)

target_include_directories(assetlib PRIVATE ${Stb_INCLUDE_DIR} ${TINYGLTF_INCLUDE_DIRS})

enable_strict_compiler(assetlib)

file(GLOB_RECURSE ASSETLIB_CLI_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/cli/*.c
)

file(GLOB_RECURSE ASSETLIB_CLI_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/cli/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/cli/*.hpp
)

add_executable(assetlib_cli
	${ASSETLIB_CLI_SOURCES}
	${ASSETLIB_CLI_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(assetlib_cli
	PRIVATE
		assetlib
		CLI11::CLI11
)

target_precompile_headers(assetlib_cli
	PUBLIC ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

set_target_properties(assetlib_cli PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/cli" PREFIX "Source Files" FILES ${ASSETLIB_CLI_SOURCES} ${ASSETLIB_CLI_HEADERS})

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE ASSETLIB_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(assetlib_tests
		${ASSETLIB_TESTS_SOURCES}
	)

	target_include_directories(assetlib_tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/include
			${CMAKE_CURRENT_SOURCE_DIR}/src
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
	)

	target_force_include(assetlib_tests
		PRIVATE "${CMAKE_SOURCE_DIR}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
	)

	target_link_libraries(assetlib_tests
		PRIVATE
			assetlib
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(assetlib_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(assetlib_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	copy_to_target(assetlib_tests ITEMS ${CMAKE_SOURCE_DIR}/assets)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${ASSETLIB_TESTS_SOURCES})

	enable_strict_compiler(assetlib_tests)
endif()
