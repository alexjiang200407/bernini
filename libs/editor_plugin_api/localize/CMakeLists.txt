# Localize, compiled once: every plugin and the host link it, and none compiles std::format for it.
# Separate from editor_localization so a plugin links the formatting and not the host's resolver.
file(GLOB_RECURSE EDITOR_LOCALIZE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(editor_localize STATIC ${EDITOR_LOCALIZE_SOURCES})
target_include_directories(editor_localize PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(editor_localize PUBLIC Qt6::Core)
set_target_properties(editor_localize PROPERTIES
    AUTOMOC OFF AUTOUIC OFF DISABLE_PRECOMPILE_HEADERS ON)
enable_strict_compiler(editor_localize)
enable_coverage(editor_localize)
