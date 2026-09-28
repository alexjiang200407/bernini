# metal-cpp comes from device_context, which emits its symbols once for the whole process and
# carries the headers on its public include path.
file(GLOB_RECURSE BGL_METAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE BGL_METAL_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(bgl_metal OBJECT
    ${BGL_METAL_SOURCES}
    ${BGL_METAL_HEADERS}
)

find_package(slang CONFIG REQUIRED)

target_include_directories(bgl_metal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(bgl_metal
    PRIVATE ${BERNINI_ROOT}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(bgl_metal PRIVATE RENDERER_BACKEND_METAL)

target_link_libraries(bgl_metal
    PUBLIC bgl bgl_common device_context core assetlib_structs glm::glm slang::slang
    PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore"
)
