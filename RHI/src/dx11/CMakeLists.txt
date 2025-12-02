file(GLOB_RECURSE RHI_DX11_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

target_link_libraries(rhi PRIVATE d3d11 dxgi)

target_sources(rhi PRIVATE ${RHI_DX11_SOURCES})

target_precompile_headers(rhi
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dx_pch.h
)
