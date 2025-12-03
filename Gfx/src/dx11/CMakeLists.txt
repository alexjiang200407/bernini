message(STATUS "Compiling for DX11")

file(GLOB_RECURSE DX11_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE DX11_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

target_link_libraries(gfx PRIVATE d3d11 dxgi nvrhi_d3d11)

target_compile_definitions(gfx PUBLIC RENDERER_DX11)

target_precompile_headers(gfx
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_sources(gfx PRIVATE ${DX11_SOURCES} ${DX11_HEADERS})

target_compile_definitions(gfx PRIVATE RENDERER_BACKEND_DX11)

target_include_directories(gfx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
