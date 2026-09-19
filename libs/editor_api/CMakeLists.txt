add_library(editor_api INTERFACE)
target_include_directories(editor_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(editor_api INTERFACE assetlib_plugin_api gamelib Qt6::Widgets)

add_subdirectory(localization)

file(GLOB_RECURSE EDITOR_API_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
foreach(header IN LISTS EDITOR_API_HEADERS)
    file(RELATIVE_PATH relative_header ${CMAKE_CURRENT_SOURCE_DIR}/include ${header})
    string(MAKE_C_IDENTIFIER "${relative_header}" check_name)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${check_name}.cpp")
    file(GENERATE OUTPUT ${check_source} CONTENT "#include <${relative_header}>\n")
    list(APPEND EDITOR_API_CHECKS ${check_source})
endforeach()
add_library(editor_api_selfcheck STATIC ${EDITOR_API_CHECKS})
target_link_libraries(editor_api_selfcheck PRIVATE editor_api)
set_target_properties(editor_api_selfcheck PROPERTIES
    AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
