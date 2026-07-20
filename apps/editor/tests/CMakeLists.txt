find_package(Catch2 CONFIG REQUIRED)

# Qt6::Test is linked but is *not* the test framework: Catch2 is. What comes from it is QSignalSpy
# and the input simulation (QTest::mouseClick), both of which are plain Qt classes and functions and
# work fine inside a Catch2 TEST_CASE.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Test)

file(GLOB_RECURSE EDITOR_TESTS_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

# Not qt_add_executable: that would apply WIN32_EXECUTABLE-style finalization, and a test binary
# needs a console to report to.
add_executable(editor_tests
	${EDITOR_TESTS_SOURCES}
)

# AUTOMOC off: nothing here declares a Q_OBJECT. The tests are plain Catch2 TEST_CASEs.
set_target_properties(editor_tests PROPERTIES
	AUTOMOC OFF
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

target_include_directories(editor_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

# editor_lib carries the editor's own objects, its include dirs, its PCH and its Qt/bgl/assetlib link
# line, so the tests compile against exactly what ships.
#
# Catch2::Catch2 without Catch2WithMain: main() lives in src/main.cpp, because a QApplication has to
# exist before any test runs.
target_link_libraries(editor_tests PRIVATE
	editor_lib
	Catch2::Catch2
	Qt6::Test
)

if(RENDERER_BACKEND STREQUAL "DX12")
	target_link_libraries(editor_tests PRIVATE bgl_d3d12_agility)
endif()

target_precompile_headers(editor_tests
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

# The tests read fixtures out of the shared asset directory, which copy_assets stages into the
# runtime output dir they run from.
add_dependencies(editor_tests copy_assets)

if(WIN32)
	# editor.exe's windeployqt does not know about Qt6Test.dll, so ask for the test binary's own
	# runtime. Same output directory, so this mostly no-ops on what editor already deployed.
	add_custom_command(TARGET editor_tests POST_BUILD
		COMMAND Qt6::windeployqt
			--no-translations
			--no-compiler-runtime
			"$<TARGET_FILE:editor_tests>"
		VERBATIM
		COMMENT "windeployqt: copying Qt runtime next to editor_tests.exe"
	)

	# windeployqt ships only the platform plugin the binary would use interactively, and the suite
	# defaults itself to `offscreen` so its widgets never take focus. Deploy that plugin too, and
	# tell main.cpp it is there -- naming a platform Qt cannot load aborts with a modal error box.
	if(TARGET Qt6::QOffscreenIntegrationPlugin)
		add_custom_command(TARGET editor_tests POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
				"$<TARGET_FILE_DIR:editor_tests>/platforms/"
			VERBATIM
			COMMENT "copying Qt offscreen platform plugin next to editor_tests.exe"
		)
		target_compile_definitions(editor_tests PRIVATE EDITOR_TESTS_HAVE_OFFSCREEN)
	endif()
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${EDITOR_TESTS_SOURCES})
