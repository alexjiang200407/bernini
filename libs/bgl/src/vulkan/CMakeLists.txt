# The renderer's Vulkan half: the swapchain, with the Graphics and render target every backend that
# presents through an ISwapchain shares (src/swapchain), which bgl_objects leaves out. Vulkan is
# reached through volk, in a copy of its own under namespace volk: bgpu's pointers are not exported
# from a shared bgpu, and a second unnamespaced copy would collide with them in a static one.
file(GLOB_RECURSE BGL_VULKAN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../swapchain/*.cpp
)
file(GLOB_RECURSE BGL_VULKAN_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../swapchain/*.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/.." PREFIX "Source Files" FILES ${BGL_VULKAN_SOURCES} ${BGL_VULKAN_HEADERS})

add_library(bgl_vulkan OBJECT
    ${BGL_VULKAN_SOURCES}
    ${BGL_VULKAN_HEADERS}
)

find_package(volk CONFIG REQUIRED)

target_include_directories(bgl_vulkan
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(bgl_vulkan
    PRIVATE ${BERNINI_ROOT}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(bgl_vulkan PRIVATE RENDERER_BACKEND_VULKAN VOLK_NAMESPACE)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/VolkImpl.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(bgl_vulkan
    PUBLIC bgl_headers bgl_idl bgpu core assetlib_structs glm::glm
    PRIVATE volk::volk_headers
)
