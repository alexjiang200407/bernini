file(GLOB_RECURSE BGL_CROWD_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(bgl_crowd ${BGL_CROWD_SOURCES})

find_package(CLI11 CONFIG REQUIRED)

# example_headless for loading a skinned character from a project (--project, --import): it brings
# gamelib and assetlib, which an example may link beside the crowd, never the crowd's own libraries.
target_link_libraries(bgl_crowd PRIVATE core bgpu bgl crowdlib crowd_render CLI11::CLI11 example_util
    example_headless)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_crowd PRIVATE bgpu_d3d12_agility)
endif()

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
