message(STATUS "Compiling for DX11")

file(GLOB_RECURSE DX11_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE DX11_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

target_sources(gfx_objects PRIVATE 
    ${DX11_SOURCES} 
    ${DX11_HEADERS}
)

target_include_directories(gfx_objects PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_precompile_headers(gfx_objects PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_link_libraries(gfx_objects PUBLIC 
    d3d11
    dxgi
    nvrhi_d3d11 
    d3dcompiler
)

target_compile_definitions(gfx_objects PUBLIC 
    RENDERER_DX11
)
