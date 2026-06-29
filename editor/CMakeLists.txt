find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

message(STATUS "Building Bernini editor")
message(STATUS "Qt6 version: ${Qt6_VERSION}")

qt_standard_project_setup()   # enables AUTOMOC / AUTOUIC / AUTORCC

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE EDITOR_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

file(GLOB_RECURSE EDITOR_UI CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/qt/*.ui
)

qt_add_executable(editor
	${EDITOR_SOURCES}
	${EDITOR_HEADERS}
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
	AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/qt"
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
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Header Files" FILES ${EDITOR_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/qt" PREFIX "UI Files" FILES ${EDITOR_UI})
