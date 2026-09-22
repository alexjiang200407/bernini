find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

message(STATUS "Building Bernini editor")
message(STATUS "Qt6 version: ${Qt6_VERSION}")

qt_standard_project_setup()   # enables AUTOMOC / AUTORCC

add_subdirectory(${BERNINI_ROOT}/libs/editor_plugin_api ${CMAKE_BINARY_DIR}/libs/editor_plugin_api)

find_package(nlohmann_json CONFIG REQUIRED)   # project file (de)serialization

add_subdirectory(plugins/default_editor)

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

# `main.cpp` is the one thing editor_tests must NOT get: it owns main(), and the test runner has its
# own. Everything else the editor is made of lives in editor_lib, so the tests exercise the very
# objects that ship rather than a recompiled copy free to drift from them.
set(EDITOR_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM EDITOR_SOURCES "${EDITOR_MAIN}")

# An OBJECT library, not STATIC: Qt classes are reached through the meta-object system as often as
# through the linker, and a static archive is free to drop an object whose symbols look unreferenced
# -- taking its moc registration with it.
qt_add_library(editor_lib OBJECT
	${EDITOR_SOURCES}
)

target_include_directories(editor_lib PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (APPLE)
	# Objective-C++ so the Qt view can be given a CAMetalLayer; globbed separately because the
	# .cpp glob above must not pick it up on any other platform.
	file(GLOB_RECURSE EDITOR_SOURCES_MAC CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm
	)
	target_sources(editor_lib PRIVATE ${EDITOR_SOURCES_MAC})
	# The PCH is compiled as C++; Objective-C++ cannot consume it.
	set_source_files_properties(${EDITOR_SOURCES_MAC} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
	target_link_libraries(editor_lib PRIVATE "-framework AppKit" "-framework QuartzCore")
endif()

target_link_libraries(editor_lib
	PUBLIC
		core
		bgl_extended
		assetlib
		gamelib
		editor_plugin_api
		editor_sdk
		default_editor
		editor_localization
		Qt6::Widgets
		nlohmann_json::nlohmann_json
)

if(BERNINI_EDITOR_SDK)
	add_dependencies(editor_lib editor_sdk_stamp)
	set(EDITOR_BUILD_ID "${BERNINI_EDITOR_BUILD_ID}")
	set(EDITOR_SDK_STAMP "${BERNINI_EDITOR_SDK_STAMP}")
else()
	set(EDITOR_BUILD_ID "")
	set(EDITOR_SDK_STAMP "")
endif()

set(EDITOR_BUILD_CONFIG_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/$<CONFIG>")
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/src/Plugins/plugin_build_config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/plugin_build_config.h.in
	@ONLY)
file(GENERATE
	OUTPUT "${EDITOR_BUILD_CONFIG_INCLUDE_DIR}/plugin_build_config.h"
	INPUT "${CMAKE_CURRENT_BINARY_DIR}/plugin_build_config.h.in")
target_include_directories(editor_lib PRIVATE "${EDITOR_BUILD_CONFIG_INCLUDE_DIR}")


target_precompile_headers(editor_lib
	PUBLIC
		${BERNINI_ROOT}/PCH/pch.h
		${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

# No enable_strict_compiler here -- Qt and moc output trip the strict set -- but instrumentation
# is not a warning.
enable_coverage(editor_lib)

qt_add_executable(editor
	${EDITOR_MAIN}
)

target_link_libraries(editor PRIVATE editor_lib)

# The editor reads its IBL pair, BRDF LUT and skybox from the staged asset directory at startup, so it
# must depend on the staging itself -- the Release presets configure away the test and example targets
# that otherwise pull copy_assets in, and without them nothing stages assets at all.
add_dependencies(editor copy_assets)

# The editor loads every plugin under plugins/ beside its executable, so the sample the test tree
# builds is staged there and `just run editor` finds it with nothing configured. The directory is
# named by the plugin ID, which is what the loader's error names when a project wants one.
if(TARGET editor_plugin_sample)
	add_custom_target(editor_stage_sample_plugin
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${EDITOR_PLUGIN_SAMPLE_BUILD}/plugin/$<CONFIG>"
			"$<TARGET_FILE_DIR:editor>/plugins/sample.document"
		COMMENT "Staging the sample plugin beside the editor"
		VERBATIM)
	add_dependencies(editor_stage_sample_plugin editor_plugin_sample)
	add_dependencies(editor editor_stage_sample_plugin)
endif()

# On the executable, NOT on editor_lib. The Agility SDK loader looks up D3D12SDKVersion /
# D3D12SDKPath as exports of the .exe, and bgl_d3d12_agility is an OBJECT library -- CMake does not
# carry one object library's object files through another, so linking it to editor_lib (also OBJECT)
# drops it on the floor. The exports go missing, D3D12 falls back to the system runtime, and
# D3D12CreateDevice fails with E_INVALIDARG.
if(RENDERER_BACKEND STREQUAL "DX12")
	target_link_libraries(editor PRIVATE bgl_d3d12_agility)
endif()

set_target_properties(editor PROPERTIES
	WIN32_EXECUTABLE ON
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

if(WIN32)
	add_custom_command(TARGET editor POST_BUILD
		COMMAND Qt6::windeployqt
			--no-translations
			--no-compiler-runtime
			"$<TARGET_FILE:editor>"
		VERBATIM
		COMMENT "windeployqt: copying Qt runtime next to editor.exe"
	)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.json")
	set(_editor_config_src "${CMAKE_CURRENT_SOURCE_DIR}/config.json")
else()
	set(_editor_config_src "${CMAKE_CURRENT_SOURCE_DIR}/config.example.json")
endif()

add_custom_command(TARGET editor POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${_editor_config_src}"
			"$<TARGET_FILE_DIR:editor>/config.json"
	VERBATIM
	COMMENT "Deploying ${_editor_config_src} as config.json next to editor.exe"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${EDITOR_SOURCES} ${EDITOR_MAIN})

if(BUILD_TESTS)
	add_subdirectory(tests)
endif()
