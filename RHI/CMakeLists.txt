file(GLOB_RECURSE RHI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.h

)

file(GLOB RHI_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

add_library(rhi STATIC
    ${RHI_SOURCES}
    ${RHI_HEADERS}
)

target_link_libraries(rhi PRIVATE Core)

target_precompile_headers(rhi
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

target_include_directories(rhi
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (RENDERER_BACKEND STREQUAL "DX11")
    add_subdirectory(src/dx11)
else()
    message(FATAL_ERROR "Unknown backend: ${RENDERER_BACKEND}")
endif()

target_sources(rhi PRIVATE ${RHI_SOURCES} ${RHI_HEADERS})
