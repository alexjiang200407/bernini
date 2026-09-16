file(GLOB_RECURSE BGL_DIRECTIONAL_LIGHT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_DIRECTIONAL_LIGHT_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_directional_light
    ${BGL_DIRECTIONAL_LIGHT_SOURCES}
    ${BGL_DIRECTIONAL_LIGHT_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_directional_light PRIVATE core bgl_extended gamelib assetlib CLI11::CLI11 example_util example_headless)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_directional_light PRIVATE bgl_d3d12_agility Shell32)
endif()

enable_strict_compiler(bgl_directional_light)

target_precompile_headers(bgl_directional_light
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

add_dependencies(bgl_directional_light copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_DIRECTIONAL_LIGHT_SOURCES} ${BGL_DIRECTIONAL_LIGHT_HEADERS}
)
