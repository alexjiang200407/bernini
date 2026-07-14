find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

message(STATUS "Building Bernini editor")
message(STATUS "Qt6 version: ${Qt6_VERSION}")

qt_standard_project_setup()   # enables AUTOMOC / AUTOUIC / AUTORCC

find_package(nlohmann_json CONFIG REQUIRED)   # project file (de)serialization

# QtNodes (node-graph UI for the Material Editor). Vendored via FetchContent so it builds against
# the Qt already resolved by find_package(Qt6) above -- NOT vcpkg's qtbase: the vcpkg nodeeditor
# port would build all of Qt from source and collide with the system Qt. It builds a small
# QtNodes.dll (~1 MB) into the shared runtime output dir, so it sits next to editor.exe with no
# extra deploy step. Exposes the target QtNodes::QtNodes.
include(FetchContent)
set(QT_NODES_DEVELOPER_DEFAULTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	QtNodes
	GIT_REPOSITORY https://github.com/paceholder/nodeeditor.git
	GIT_TAG 3.0.16
)
FetchContent_MakeAvailable(QtNodes)

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

file(GLOB_RECURSE EDITOR_UI CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/qt/*.ui
)

# AUTOUIC_SEARCH_PATHS is not recursive
set(EDITOR_UI_DIRS "")
foreach(_ui IN LISTS EDITOR_UI)
	get_filename_component(_ui_dir "${_ui}" DIRECTORY)
	list(APPEND EDITOR_UI_DIRS "${_ui_dir}")
endforeach()
list(REMOVE_DUPLICATES EDITOR_UI_DIRS)

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
	${EDITOR_UI}
)

target_include_directories(editor_lib PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

# The editor's headers `#include "ui_<Name>.h"`, so anything that includes one -- editor_tests --
# needs AUTOUIC's output on its include path. That lands in editor_lib's own autogen directory,
# which is private to it by default, so publish it.
#
# Multi-config generators (Visual Studio) suffix the directory with the configuration; single-config
# ones (Ninja) do not. The test is GENERATOR_IS_MULTI_CONFIG and not CMAKE_CONFIGURATION_TYPES: the
# presets set the latter even for Ninja, which would name a directory AUTOUIC never writes to.
get_property(EDITOR_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(EDITOR_MULTI_CONFIG)
	set(EDITOR_AUTOGEN_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/editor_lib_autogen/include_$<CONFIG>")
else()
	set(EDITOR_AUTOGEN_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/editor_lib_autogen/include")
endif()

target_include_directories(editor_lib PUBLIC "${EDITOR_AUTOGEN_INCLUDE}")

target_link_libraries(editor_lib
	PUBLIC
		core
		bgl
		assetlib
		gamelib
		Qt6::Widgets
		QtNodes::QtNodes
		nlohmann_json::nlohmann_json
)


target_precompile_headers(editor_lib
	PUBLIC
		${CMAKE_SOURCE_DIR}/PCH/pch.h
		${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

set_target_properties(editor_lib PROPERTIES
	AUTOUIC_SEARCH_PATHS "${EDITOR_UI_DIRS}"
)

qt_add_executable(editor
	${EDITOR_MAIN}
)

target_link_libraries(editor PRIVATE editor_lib)

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
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/qt" PREFIX "UI Files" FILES ${EDITOR_UI})

if(BUILD_TESTS)
	add_subdirectory(tests)
endif()
