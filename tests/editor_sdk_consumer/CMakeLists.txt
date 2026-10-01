cmake_minimum_required(VERSION 3.24)
project(BerniniEditorSDKConsumer LANGUAGES CXX)

find_package(BerniniEditorSDK CONFIG REQUIRED)

add_library(editor_sdk_fixture SHARED plugin.cpp)
target_link_libraries(editor_sdk_fixture PRIVATE Bernini::editor_plugin_api Bernini::editor_sdk)
target_compile_features(editor_sdk_fixture PRIVATE cxx_std_23)
set_target_properties(editor_sdk_fixture PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}")

add_library(editor_sdk_collision_fixture SHARED plugin.cpp)
target_link_libraries(editor_sdk_collision_fixture PRIVATE Bernini::editor_plugin_api Bernini::editor_sdk)
target_compile_features(editor_sdk_collision_fixture PRIVATE cxx_std_23)
set_target_properties(editor_sdk_collision_fixture PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}")

add_library(editor_sdk_rejected_fixture SHARED plugin.cpp)
target_link_libraries(editor_sdk_rejected_fixture PRIVATE Bernini::editor_plugin_api Bernini::editor_sdk)
target_compile_features(editor_sdk_rejected_fixture PRIVATE cxx_std_23)
set_target_properties(editor_sdk_rejected_fixture PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EDITOR_SDK_OUTPUT_DIR}")

# Relinked whenever the SDK is rebuilt, so each fixture is newer than the stamp the loader
# compares it against. Ordering alone is not enough: a build that moves the stamp without
# touching plugin.cpp would otherwise leave these at their previous link time.
foreach(target editor_sdk_fixture editor_sdk_collision_fixture editor_sdk_rejected_fixture)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${BerniniEditorSDK_DIR}/bernini-sdk.stamp")
endforeach()
