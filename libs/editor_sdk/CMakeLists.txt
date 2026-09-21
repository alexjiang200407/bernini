set(EDITOR_SDK_LIBRARY_TYPE STATIC)
if(BERNINI_EDITOR_SDK)
    set(EDITOR_SDK_LIBRARY_TYPE SHARED)
endif()

file(GLOB_RECURSE EDITOR_SDK_SOURCES CONFIGURE_DEPENDS src/*.cpp include/*.h)
add_library(editor_sdk ${EDITOR_SDK_LIBRARY_TYPE} ${EDITOR_SDK_SOURCES})
include(GenerateExportHeader)
generate_export_header(editor_sdk
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/editor_sdk/export.h)
if(NOT BERNINI_EDITOR_SDK)
    target_compile_definitions(editor_sdk PUBLIC EDITOR_SDK_STATIC_DEFINE)
endif()
target_include_directories(editor_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(editor_sdk PUBLIC assetlib bgl Qt6::Widgets)
set_target_properties(editor_sdk PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
enable_strict_compiler(editor_sdk)
enable_coverage(editor_sdk)

file(GLOB_RECURSE EDITOR_SDK_HEADERS CONFIGURE_DEPENDS include/*.h)
foreach(header IN LISTS EDITOR_SDK_HEADERS)
    file(RELATIVE_PATH relative_header ${CMAKE_CURRENT_SOURCE_DIR}/include ${header})
    string(MAKE_C_IDENTIFIER "${relative_header}" check_name)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${check_name}.cpp")
    file(GENERATE OUTPUT ${check_source} CONTENT "#include <${relative_header}>\n")
    list(APPEND EDITOR_SDK_CHECKS ${check_source})
endforeach()
add_library(editor_sdk_selfcheck STATIC ${EDITOR_SDK_CHECKS})
target_link_libraries(editor_sdk_selfcheck PRIVATE editor_sdk)
set_target_properties(editor_sdk_selfcheck PROPERTIES
    AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
