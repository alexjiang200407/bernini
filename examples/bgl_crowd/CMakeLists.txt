file(GLOB_RECURSE BGL_CROWD_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(bgl_crowd ${BGL_CROWD_SOURCES})

find_package(CLI11 CONFIG REQUIRED)

# `headless` for loading a skinned character from a project (--project, --import): it brings
# gamelib and assetlib, which an example may link beside the crowd, never the crowd's own libraries.
#
# gamelib ahead of example_util, as bgl_ui has it: gamelib holds the process's one RmlUi, and
# example_util's static RmlUi listed first would hand the frame rate overlay's Rml calls a second
# copy, whose globals Rml::Initialise never sets -- a crash at start.
target_link_libraries(bgl_crowd PRIVATE core bgpu bgl crowdlib crowd_render gamelib headless
    CLI11::CLI11 example_util)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_crowd PRIVATE bgpu_d3d12_agility)
endif()

# The frame rate overlay's font is the engine's bundled data, staged beside the executable.
add_dependencies(bgl_crowd copy_assets)

enable_strict_compiler(bgl_crowd)

target_precompile_headers(bgl_crowd
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_CROWD_SOURCES}
)
