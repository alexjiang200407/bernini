file(GLOB_RECURSE DX11_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE DX11_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

target_sources(Renderer PRIVATE ${DX11_SOURCES} ${DX11_HEADERS})

target_precompile_headers(Renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dx_pch.h
)

target_compile_definitions(Renderer PRIVATE RENDERER_BACKEND_DX11)

target_include_directories(Renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    Renderer
    PRIVATE
        d3d11
        dxgi
)
