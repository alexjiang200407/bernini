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

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${BGL_DX12_SOURCES} ${BGL_DX12_HEADERS})

add_library(bgl_d3d12 OBJECT
    ${BGL_DX12_SOURCES}
    ${BGL_DX12_HEADERS}
)

find_package(directx-headers CONFIG REQUIRED)

find_package(directxtex CONFIG REQUIRED)

find_package(winpixevent CONFIG REQUIRED)

target_include_directories(bgl_d3d12
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(bgl_d3d12
    PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(bgl_d3d12 PRIVATE RENDERER_BACKEND_DX12 $<$<CONFIG:Debug>:USE_PIX>)

target_link_libraries(bgl_d3d12
    PUBLIC core assetlib_structs glm::glm
    PRIVATE d3d12 dxgi dxguid Microsoft::DirectX-Headers Microsoft::DirectXTex
            Microsoft::WinPixEventRuntime
)

add_library(bgl_d3d12_agility OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/agility/D3D12Agility.cpp
)
