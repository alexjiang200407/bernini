add_library(editor_plugin_api INTERFACE)
target_include_directories(editor_plugin_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(localize)
target_link_libraries(editor_plugin_api INTERFACE assetlib_plugin_api gamelib editor_localize Qt6::Widgets)

add_subdirectory(localization)
add_subdirectory(${BERNINI_ROOT}/libs/editor_sdk ${CMAKE_BINARY_DIR}/libs/editor_sdk)

file(GLOB_RECURSE EDITOR_PLUGIN_API_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
foreach(header IN LISTS EDITOR_PLUGIN_API_HEADERS)
    file(RELATIVE_PATH relative_header ${CMAKE_CURRENT_SOURCE_DIR}/include ${header})
    string(MAKE_C_IDENTIFIER "${relative_header}" check_name)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${check_name}.cpp")
    file(GENERATE OUTPUT ${check_source} CONTENT "#include <${relative_header}>\n")
    list(APPEND EDITOR_PLUGIN_API_CHECKS ${check_source})
endforeach()
add_library(editor_plugin_api_selfcheck STATIC ${EDITOR_PLUGIN_API_CHECKS})
target_link_libraries(editor_plugin_api_selfcheck PRIVATE editor_plugin_api)
set_target_properties(editor_plugin_api_selfcheck PROPERTIES
    AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)

if(BERNINI_EDITOR_SDK)
    file(GLOB_RECURSE BERNINI_EDITOR_SDK_PUBLIC_HEADERS CONFIGURE_DEPENDS
        "${BERNINI_ROOT}/libs/core/include/*.h"
        "${BERNINI_ROOT}/libs/assetlib_structs/include/*.h"
        "${BERNINI_ROOT}/libs/bgl/include/*.h"
        "${BERNINI_ROOT}/libs/assetlib/include/*.h"
        "${BERNINI_ROOT}/libs/gamelib/include/*.h"
        "${BERNINI_ROOT}/libs/editor_plugin_api/include/*.h"
        "${BERNINI_ROOT}/libs/editor_sdk/include/*.h")
    set(BERNINI_EDITOR_SDK_DIR "${CMAKE_BINARY_DIR}/editor_sdk" CACHE INTERNAL
        "Build-tree package directory for native editor plugins")
    file(MAKE_DIRECTORY "${BERNINI_EDITOR_SDK_DIR}")
    export(TARGETS
        core_process core assetlib_structs bgl assetlib assetlib_plugin_api gamelib editor_localize editor_plugin_api editor_sdk
        NAMESPACE Bernini::
        FILE "${BERNINI_EDITOR_SDK_DIR}/BerniniEditorSDKTargets.cmake")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/BerniniEditorSDKConfig.cmake.in"
        "${BERNINI_EDITOR_SDK_DIR}/BerniniEditorSDKConfig.cmake"
        @ONLY)

    set(BERNINI_EDITOR_SDK_STAMP "${BERNINI_EDITOR_SDK_DIR}/bernini-sdk.stamp"
        CACHE INTERNAL "Newest build of the libraries exposed by the editor SDK")
    add_custom_command(
        OUTPUT "${BERNINI_EDITOR_SDK_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E touch "${BERNINI_EDITOR_SDK_STAMP}"
        DEPENDS core_process assetlib gamelib editor_localize editor_sdk ${BERNINI_EDITOR_SDK_PUBLIC_HEADERS}
        VERBATIM)
    add_custom_target(editor_sdk_stamp DEPENDS "${BERNINI_EDITOR_SDK_STAMP}")
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
