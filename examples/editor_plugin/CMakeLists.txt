cmake_minimum_required(VERSION 3.24)

# Its own project either way: standalone it is the whole build, and under the engine's test tree it
# is a fixture, which PROJECT_IS_TOP_LEVEL tells apart without naming the build's root.
project(BerniniSamplePlugin LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    find_package(BerniniEditorSDK CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)

    add_library(sample_runtime SHARED runtime.cpp runtime_entry.cpp)
    target_link_libraries(sample_runtime PRIVATE Bernini::assetlib_plugin_api nlohmann_json::nlohmann_json)
    add_library(sample_editor SHARED sample.cpp editor_entry.cpp)
    target_link_libraries(sample_editor PRIVATE Bernini::editor_plugin_api)
    add_executable(sample_readback readback.cpp runtime.cpp)
    target_link_libraries(sample_readback PRIVATE Bernini::assetlib nlohmann_json::nlohmann_json)

    foreach(target sample_runtime sample_editor sample_readback)
        target_compile_features(${target} PRIVATE cxx_std_20)
        set_target_properties(${target} PROPERTIES
            PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugin/$<CONFIG>"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugin/$<CONFIG>"
            DISABLE_PRECOMPILE_HEADERS ON)
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8)
        endif()
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${BerniniEditorSDK_DIR}/bernini-sdk.stamp")
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/plugin/$<CONFIG>/bernini-plugin.json" CONTENT
"{\n  \"version\": 1,\n  \"id\": \"sample.document\",\n  \"name\": \"Sample Document\",\n  \"description\": \"A .bexample document kind with a project tools tab and a document tab.\",\n  \"engineBuildId\": \"${BerniniEditorSDK_BUILD_ID}\",\n  \"configuration\": \"$<CONFIG>\",\n  \"runtime\": \"$<TARGET_FILE_NAME:sample_runtime>\",\n  \"editor\": \"$<TARGET_FILE_NAME:sample_editor>\",\n  \"dependencies\": []\n}\n")
    return()
endif()

find_package(nlohmann_json CONFIG REQUIRED)
add_library(editor_plugin_example STATIC sample.cpp runtime.cpp)
target_include_directories(editor_plugin_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_plugin_example PUBLIC editor_plugin_api PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(editor_plugin_example PROPERTIES AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
