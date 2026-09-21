# QtNodes (node-graph UI for the Material Editor). Vendored via FetchContent so it builds against
# the Qt already resolved by find_package(Qt6) above -- NOT vcpkg's qtbase: the vcpkg nodeeditor
# port would build all of Qt from source and collide with the system Qt. It builds a small
# QtNodes.dll (~1 MB) into the shared runtime output dir, so it sits next to editor.exe with no
# extra deploy step. Exposes the target QtNodes::QtNodes.
include(FetchContent)
set(QT_NODES_DEVELOPER_DEFAULTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	QtNodes
	GIT_REPOSITORY https://github.com/paceholder/nodeeditor.git
	GIT_TAG 3.0.16
)
FetchContent_MakeAvailable(QtNodes)

# Qt's FindWrapOpenGL adds AGL to everything linking Qt6::OpenGL, and finds it by searching the
# running system rather than the SDK. AGL.framework still exists on disk but was dropped from the
# SDK, so the path resolves at configure time and then fails at link under -isysroot. Nothing here
# wants the Carbon-era API; OpenGL.framework is what a modern build links, so AGL is filtered out.
if (APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_bernini_wrapgl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if (_bernini_wrapgl_libs)
        list(FILTER _bernini_wrapgl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_bernini_wrapgl_libs}")
    endif()
endif()

file(GLOB_RECURSE DEFAULT_EDITOR_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h include/*.h)
add_library(default_editor STATIC ${DEFAULT_EDITOR_SOURCES})
target_include_directories(default_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(default_editor PUBLIC editor_api editor_sdk PRIVATE QtNodes::QtNodes nlohmann_json::nlohmann_json)
enable_coverage(default_editor)

set_target_properties(default_editor PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
