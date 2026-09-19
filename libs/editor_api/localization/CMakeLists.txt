file(GLOB_RECURSE EDITOR_LOCALIZATION_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(editor_localization STATIC ${EDITOR_LOCALIZATION_SOURCES})
target_include_directories(editor_localization PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(editor_localization PUBLIC core Qt6::Core)
set_target_properties(editor_localization PROPERTIES
    AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
enable_strict_compiler(editor_localization)
enable_coverage(editor_localization)
