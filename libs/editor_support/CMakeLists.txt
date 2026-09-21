set(EDITOR_SUPPORT_LIBRARY_TYPE STATIC)
if(BERNINI_EDITOR_SDK)
    set(EDITOR_SUPPORT_LIBRARY_TYPE SHARED)
endif()

file(GLOB_RECURSE EDITOR_SUPPORT_SOURCES CONFIGURE_DEPENDS src/*.cpp include/*.h)
add_library(editor_support ${EDITOR_SUPPORT_LIBRARY_TYPE} ${EDITOR_SUPPORT_SOURCES})
include(GenerateExportHeader)
generate_export_header(editor_support
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/editor_support/export.h)
if(NOT BERNINI_EDITOR_SDK)
    target_compile_definitions(editor_support PUBLIC EDITOR_SUPPORT_STATIC_DEFINE)
endif()
target_include_directories(editor_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(editor_support PUBLIC assetlib bgl Qt6::Widgets)
set_target_properties(editor_support PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
enable_strict_compiler(editor_support)
enable_coverage(editor_support)

file(GLOB_RECURSE EDITOR_SUPPORT_HEADERS CONFIGURE_DEPENDS include/*.h)
foreach(header IN LISTS EDITOR_SUPPORT_HEADERS)
    file(RELATIVE_PATH relative_header ${CMAKE_CURRENT_SOURCE_DIR}/include ${header})
    string(MAKE_C_IDENTIFIER "${relative_header}" check_name)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${check_name}.cpp")
    file(GENERATE OUTPUT ${check_source} CONTENT "#include <${relative_header}>\n")
    list(APPEND EDITOR_SUPPORT_CHECKS ${check_source})
endforeach()
add_library(editor_support_selfcheck STATIC ${EDITOR_SUPPORT_CHECKS})
target_link_libraries(editor_support_selfcheck PRIVATE editor_support)
set_target_properties(editor_support_selfcheck PROPERTIES
    AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
