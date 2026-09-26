file(GLOB_RECURSE BGL_GRASS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_GRASS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_grass
    ${BGL_GRASS_SOURCES}
    ${BGL_GRASS_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_grass PRIVATE core bgl_extended gamelib assetlib CLI11::CLI11 example_util example_headless)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_grass PRIVATE bgl_d3d12_agility Shell32)
endif()

enable_strict_compiler(bgl_grass)

target_precompile_headers(bgl_grass
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

add_dependencies(bgl_grass copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_GRASS_SOURCES} ${BGL_GRASS_HEADERS}
)
