find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

message(STATUS "Building Bernini editor")
message(STATUS "Qt6 version: ${Qt6_VERSION}")

qt_standard_project_setup()   # enables AUTOMOC / AUTOUIC / AUTORCC

find_package(nlohmann_json CONFIG REQUIRED)   # project file (de)serialization

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

qt_add_executable(editor
	${EDITOR_SOURCES}
	${EDITOR_UI}
)

target_include_directories(editor PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(editor
	PRIVATE
		core
		bgl
		assetlib
		Qt6::Widgets
		nlohmann_json::nlohmann_json
)

if(RENDERER_BACKEND STREQUAL "DX12")
	target_link_libraries(editor PRIVATE bgl_d3d12_agility)
endif()

target_precompile_headers(editor
	PUBLIC ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

set_target_properties(editor PROPERTIES
	WIN32_EXECUTABLE ON
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	AUTOUIC_SEARCH_PATHS "${EDITOR_UI_DIRS}"
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

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${EDITOR_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/qt" PREFIX "UI Files" FILES ${EDITOR_UI})
