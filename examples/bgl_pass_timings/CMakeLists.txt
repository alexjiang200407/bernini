file(GLOB_RECURSE BGL_PASS_TIMINGS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_PASS_TIMINGS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_pass_timings
    ${BGL_PASS_TIMINGS_SOURCES}
    ${BGL_PASS_TIMINGS_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_pass_timings PRIVATE core bgl_extended gamelib assetlib CLI11::CLI11)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_pass_timings PRIVATE bgl_d3d12_agility Shell32)
endif()

enable_strict_compiler(bgl_pass_timings)

target_precompile_headers(bgl_pass_timings
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

add_dependencies(bgl_pass_timings copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_PASS_TIMINGS_SOURCES} ${BGL_PASS_TIMINGS_HEADERS}
)
