file(GLOB_RECURSE BGL_SPHERE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_SPHERE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_sphere WIN32
    ${BGL_SPHERE_SOURCES}
    ${BGL_SPHERE_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_sphere PRIVATE core bgl assetlib CLI11::CLI11 example_util )

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_sphere PRIVATE bgl_d3d12_agility Shell32)
endif()

target_precompile_headers(bgl_sphere
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

add_dependencies(bgl_sphere copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_SPHERE_SOURCES} ${BGL_SPHERE_HEADERS}
)
