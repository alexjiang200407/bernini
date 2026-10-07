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

set(GAMELIB_LIBRARY_TYPE STATIC)
if (BERNINI_EDITOR_SDK)
	set(GAMELIB_LIBRARY_TYPE SHARED)
endif()

add_library(gamelib ${GAMELIB_LIBRARY_TYPE}
	${GAMELIB_SOURCES}
	${GAMELIB_SHARED_HEADERS}
	${GAMELIB_INTERNAL_HEADERS}
)

if (BERNINI_EDITOR_SDK)
	set_target_properties(gamelib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(gamelib
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (NOT (BERNINI_EDITOR_SDK AND MSVC))
	target_precompile_headers(gamelib
		PRIVATE
			${BERNINI_ROOT}/PCH/pch.h
			${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
	)
endif()

# bgl_headers for the public surface, the renderer's symbols below: by target for a static build, by
# file for an SDK one.
#
# RmlUi is PUBLIC because a client drives Rml::Context itself (ADR-7); its include directories are
# imported as SYSTEM, so enable_strict_compiler's warnings-as-errors never compile its headers.
target_link_libraries(gamelib PUBLIC core bgl_headers assetlib)

if (BERNINI_EDITOR_SDK)
	add_dependencies(gamelib bgl)
	target_link_libraries(gamelib PRIVATE "$<TARGET_LINKER_FILE:bgl>")
	target_include_directories(gamelib PRIVATE
		$<TARGET_PROPERTY:bgl_idl,INTERFACE_INCLUDE_DIRECTORIES>)

	# RmlUi and its Lua plugin hold process-wide state. Pull every object into gamelib, which owns
	# UiRuntime's one-per-process guard, and pass only their headers to plugin clients.
	foreach(RMLUI_TARGET RmlUi::Core RmlUi::Debugger RmlUi::Lua)
		get_target_property(RMLUI_TARGET_TYPE ${RMLUI_TARGET} TYPE)
		if (WIN32 AND NOT RMLUI_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
			message(FATAL_ERROR
				"BERNINI_EDITOR_SDK requires shared ${RMLUI_TARGET} on Windows")
		endif()
		if (RMLUI_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
			target_link_libraries(gamelib PUBLIC ${RMLUI_TARGET})
		else()
			target_link_libraries(gamelib PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${RMLUI_TARGET}>")
		endif()
	endforeach()
	target_include_directories(gamelib SYSTEM PUBLIC
		$<TARGET_PROPERTY:RmlUi::Core,INTERFACE_INCLUDE_DIRECTORIES>)
else()
	# A static game build has one final binary, so its client may link the archives directly.
	target_link_libraries(gamelib PUBLIC bgl RmlUi::RmlUi RmlUi::Lua)
endif()

enable_strict_compiler(gamelib)
enable_coverage(gamelib)

# The suite drives a real scene through IGraphics, which needs a backend -- either backend:
# the DX12-only gate predated the Metal one being able to stand a device up. Not on Vulkan yet,
# whose renderer has bgl_sphere alone above it (the root CMakeLists.txt).
if (BUILD_TESTS AND BERNINI_HAS_RENDERER_CONSUMERS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE GAMELIB_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	# bgl's golden-image, environment and shader-cache helpers, borrowed rather than re-grown:
	# they speak only the public bgl API plus assetlib, both of which this target links. A list of
	# their own so source_group's TREE only sees files under this directory.
	set(GAMELIB_TESTS_BORROWED
		${BERNINI_ROOT}/libs/bgl/tests/src/util/GoldenImage.cpp
		${BERNINI_ROOT}/libs/bgl/tests/src/util/GpuValidation.cpp
		${BERNINI_ROOT}/libs/bgl/tests/src/util/TestEnvironment.cpp
		${BERNINI_ROOT}/libs/bgl/tests/src/util/TestOptions.cpp
		${BERNINI_ROOT}/libs/bgl/tests/src/util/TestGraphics.cpp
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
			${BERNINI_ROOT}/libs/bgl/tests/src
			${BERNINI_ROOT}/libs/assetlib/tests/src
	)

	target_force_include(gamelib_tests
		PRIVATE "${BERNINI_ROOT}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
	)

	target_link_libraries(gamelib_tests
		PRIVATE
			gamelib
			bgl_idl
			bgl
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

	# The pixel test lights its view from "assets/Data/Environments/forest.benv", resolved from the
	# runtime output directory like every bgl test asset; copy_assets is what stages it there.
	add_dependencies(gamelib_tests copy_assets)

	enable_strict_compiler(gamelib_tests)
	enable_coverage(gamelib_tests)
endif()
