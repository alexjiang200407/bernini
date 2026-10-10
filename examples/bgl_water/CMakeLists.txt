file(GLOB_RECURSE BGL_WATER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_WATER_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_water
    ${BGL_WATER_SOURCES}
    ${BGL_WATER_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_water PRIVATE core bgl gamelib assetlib terrainlib CLI11::CLI11 example_util headless)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_water PRIVATE bgpu_d3d12_agility Shell32)
endif()

enable_strict_compiler(bgl_water)

target_precompile_headers(bgl_water
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

add_dependencies(bgl_water copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_WATER_SOURCES} ${BGL_WATER_HEADERS}
)
