file(GLOB_RECURSE BGL_GPU_ASSERT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_GPU_ASSERT_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_gpu_assert WIN32
    ${BGL_GPU_ASSERT_SOURCES}
    ${BGL_GPU_ASSERT_HEADERS}
)

target_link_libraries(bgl_gpu_assert PRIVATE core bgl)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_gpu_assert PRIVATE bgl_d3d12_agility)
endif()

target_include_directories(bgl_gpu_assert PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../util
)

target_precompile_headers(bgl_gpu_assert
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_GPU_ASSERT_SOURCES} ${BGL_GPU_ASSERT_HEADERS}
)
