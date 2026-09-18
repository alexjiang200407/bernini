find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
add_subdirectory(${BERNINI_ROOT}/examples/editor_plugin ${CMAKE_CURRENT_BINARY_DIR}/example)
add_executable(editor_plugin_tests main.cpp contracts_test.cpp)
target_link_libraries(editor_plugin_tests PRIVATE
    editor_api editor_plugin_example Catch2::Catch2 nlohmann_json::nlohmann_json)
set_target_properties(editor_plugin_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
add_dependencies(editor_plugin_tests editor_api_selfcheck assetlib_plugin_api_selfcheck)
enable_coverage(editor_plugin_tests)
if(WIN32)
    add_custom_command(TARGET editor_plugin_tests POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime
            "$<TARGET_FILE:editor_plugin_tests>"
        VERBATIM)
endif()
