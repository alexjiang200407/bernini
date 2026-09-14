file(GLOB_RECURSE BGL_AI_VIEWER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_AI_VIEWER_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_ai_viewer
    ${BGL_AI_VIEWER_SOURCES}
    ${BGL_AI_VIEWER_HEADERS}
)

find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(bgl_ai_viewer PRIVATE core bgl_extended gamelib assetlib CLI11::CLI11 example_headless)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_ai_viewer PRIVATE bgl_d3d12_agility Shell32)
endif()

enable_strict_compiler(bgl_ai_viewer)

target_precompile_headers(bgl_ai_viewer
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

add_dependencies(bgl_ai_viewer copy_assets)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_AI_VIEWER_SOURCES} ${BGL_AI_VIEWER_HEADERS}
)
