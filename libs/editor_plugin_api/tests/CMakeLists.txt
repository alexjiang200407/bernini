find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
add_subdirectory(${BERNINI_ROOT}/examples/editor_plugin ${CMAKE_CURRENT_BINARY_DIR}/example)
add_executable(editor_plugin_tests main.cpp contracts_test.cpp localization_test.cpp)
target_link_libraries(editor_plugin_tests PRIVATE
    editor_plugin_api editor_localization editor_plugin_example Catch2::Catch2 nlohmann_json::nlohmann_json)
set_target_properties(editor_plugin_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
add_dependencies(editor_plugin_tests editor_plugin_api_selfcheck assetlib_plugin_api_selfcheck editor_sdk_selfcheck)

if(BERNINI_EDITOR_SDK)
    include(ExternalProject)
    string(REPLACE ";" "|" EDITOR_SDK_PREFIX_PATH "${CMAKE_PREFIX_PATH}")
    string(REPLACE ";" "|" EDITOR_SDK_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}")
    ExternalProject_Add(editor_sdk_consumer
        SOURCE_DIR "${BERNINI_ROOT}/tests/editor_sdk_consumer"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdk_consumer"
        CMAKE_ARGS
            "-DBerniniEditorSDK_DIR=${BERNINI_EDITOR_SDK_DIR}"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DCMAKE_CONFIGURATION_TYPES=${EDITOR_SDK_CONFIGURATION_TYPES}"
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}"
            "-DCMAKE_PREFIX_PATH=${EDITOR_SDK_PREFIX_PATH}"
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
            "-DEDITOR_SDK_OUTPUT_DIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        BUILD_ALWAYS TRUE
        LIST_SEPARATOR "|"
        INSTALL_COMMAND ""
        DEPENDS assetlib gamelib core_process editor_sdk)
    target_link_libraries(editor_plugin_tests PRIVATE editor_sdk)
    add_dependencies(editor_plugin_tests editor_sdk_consumer)
    target_sources(editor_plugin_tests PRIVATE sdk_consumer_test.cpp)
    target_include_directories(editor_plugin_tests SYSTEM PRIVATE
        $<TARGET_PROPERTY:RmlUi::Core,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(editor_plugin_tests PRIVATE
        EDITOR_SDK_FIXTURE="$<TARGET_FILE_DIR:editor_plugin_tests>/editor_sdk_fixture${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(EDITOR_PLUGIN_SAMPLE_BUILD "${CMAKE_CURRENT_BINARY_DIR}/sample_plugin" CACHE INTERNAL
        "Independent local sample plugin build")
    ExternalProject_Add(editor_plugin_sample
        SOURCE_DIR "${BERNINI_ROOT}/examples/editor_plugin"
        BINARY_DIR "${EDITOR_PLUGIN_SAMPLE_BUILD}"
        CMAKE_ARGS
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
            "-DBerniniEditorSDK_DIR=${BERNINI_EDITOR_SDK_DIR}"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DCMAKE_CONFIGURATION_TYPES=${EDITOR_SDK_CONFIGURATION_TYPES}"
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}"
            "-DCMAKE_PREFIX_PATH=${EDITOR_SDK_PREFIX_PATH}"
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
        BUILD_ALWAYS TRUE
        LIST_SEPARATOR "|"
        INSTALL_COMMAND ""
        DEPENDS editor_sdk_stamp)
    add_dependencies(editor_plugin_tests editor_plugin_sample)
endif()
enable_coverage(editor_plugin_tests)
if(WIN32)
    add_custom_command(TARGET editor_plugin_tests POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime
            "$<TARGET_FILE:editor_plugin_tests>"
        VERBATIM)
endif()
