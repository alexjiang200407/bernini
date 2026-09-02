file(GLOB_RECURSE BGL_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_UI_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_ui
    ${BGL_UI_SOURCES}
    ${BGL_UI_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_ui PRIVATE core bgl_extended gamelib assetlib CLI11::CLI11 example_util )

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_ui PRIVATE bgl_d3d12_agility Shell32)
endif()

enable_strict_compiler(bgl_ui)

target_precompile_headers(bgl_ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

add_dependencies(bgl_ui copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_UI_SOURCES} ${BGL_UI_HEADERS}
)
