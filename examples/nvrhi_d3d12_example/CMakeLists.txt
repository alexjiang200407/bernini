file(GLOB_RECURSE NVRHI_D3D12_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE NVRHI_D3D12_EXAMPLE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(nvrhi_d3d12_example WIN32
    ${NVRHI_D3D12_EXAMPLE_SOURCES}
    ${NVRHI_D3D12_EXAMPLE_HEADERS}
)

target_link_libraries(nvrhi_d3d12_example PRIVATE
    Core
    d3d12
    dxgi
    nvrhi
    nvrhi_d3d12
    dxguid
    dxcompiler
)

target_precompile_headers(nvrhi_d3d12_example
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${NVRHI_D3D12_EXAMPLE_SOURCES}
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Header Files"
    FILES ${NVRHI_D3D12_EXAMPLE_HEADERS}
)


