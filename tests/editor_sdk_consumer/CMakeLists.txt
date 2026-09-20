cmake_minimum_required(VERSION 3.24)
project(BerniniEditorSDKConsumer LANGUAGES CXX)

find_package(BerniniEditorSDK CONFIG REQUIRED)

add_library(editor_sdk_fixture SHARED plugin.cpp)
target_link_libraries(editor_sdk_fixture PRIVATE Bernini::editor_api)
target_compile_features(editor_sdk_fixture PRIVATE cxx_std_20)
set_target_properties(editor_sdk_fixture PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}")

add_library(editor_sdk_collision_fixture SHARED plugin.cpp)
target_link_libraries(editor_sdk_collision_fixture PRIVATE Bernini::editor_api)
target_compile_features(editor_sdk_collision_fixture PRIVATE cxx_std_20)
set_target_properties(editor_sdk_collision_fixture PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}")

add_library(editor_sdk_rejected_fixture SHARED plugin.cpp)
target_link_libraries(editor_sdk_rejected_fixture PRIVATE Bernini::editor_api)
target_compile_features(editor_sdk_rejected_fixture PRIVATE cxx_std_20)
set_target_properties(editor_sdk_rejected_fixture PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}")
