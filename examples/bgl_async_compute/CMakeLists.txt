file(GLOB_RECURSE BGL_ASYNC_COMPUTE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(bgl_async_compute ${BGL_ASYNC_COMPUTE_SOURCES})

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_async_compute PRIVATE core bgpu bgl_extended crowdlib CLI11::CLI11 example_util)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_async_compute PRIVATE bgl_d3d12_agility)
endif()

enable_strict_compiler(bgl_async_compute)

target_precompile_headers(bgl_async_compute
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_ASYNC_COMPUTE_SOURCES}
)
