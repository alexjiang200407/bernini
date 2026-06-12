file(GLOB_RECURSE BGL_DX12_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

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
    PUBLIC Core glm::glm
    PRIVATE d3d12 dxgi dxguid Microsoft::DirectX-Headers
)
