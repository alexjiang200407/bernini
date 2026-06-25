file(GLOB_RECURSE BGL_DX12_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)
# The agility/ sources export the D3D12 Agility SDK symbols and must live in the
# consuming executable, not the bgl DLL, so keep them out of the bgl_d3d12 object
# library (see the bgl_d3d12_agility target below).
list(FILTER BGL_DX12_SOURCES EXCLUDE REGEX ".*/agility/.*")

file(GLOB_RECURSE BGL_DX12_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${BGL_DX12_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${BGL_DX12_HEADERS})

add_library(bgl_d3d12 OBJECT
    ${BGL_DX12_SOURCES}
    ${BGL_DX12_HEADERS}
)

find_package(directx-headers CONFIG REQUIRED)

# DirectXTex is used (d3d12 only) to encode screenshots/readbacks to image files.
find_package(directxtex CONFIG REQUIRED)

target_include_directories(bgl_d3d12
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(bgl_d3d12
    PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(bgl_d3d12 PRIVATE RENDERER_BACKEND_DX12)

target_link_libraries(bgl_d3d12
    PUBLIC core glm::glm
    PRIVATE d3d12 dxgi dxguid Microsoft::DirectX-Headers Microsoft::DirectXTex
)

# D3D12 Agility SDK exports. Built as a standalone OBJECT library so the
# __declspec(dllexport) symbols can be linked directly into each d3d12
# executable (object files are linked wholesale, so the unreferenced exports are
# not dropped). Executables link this via target_link_libraries(... bgl_d3d12_agility).
add_library(bgl_d3d12_agility OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/agility/D3D12Agility.cpp
)
