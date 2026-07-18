# The backend is Objective-C++ (.mm). Enabling OBJCXX makes CMake compile both the sources and the
# precompiled header in objective-c++ mode, so the Metal/Foundation framework imports parse.
enable_language(OBJCXX)

file(GLOB_RECURSE BGL_METAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE BGL_METAL_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

set_source_files_properties(${BGL_METAL_SOURCES} PROPERTIES LANGUAGE OBJCXX)

add_library(bgl_metal OBJECT
    ${BGL_METAL_SOURCES}
    ${BGL_METAL_HEADERS}
)

find_package(slang CONFIG REQUIRED)

target_include_directories(bgl_metal
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(bgl_metal
    PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(bgl_metal PRIVATE RENDERER_BACKEND_METAL)

# ARC for the Objective-C++ translation units.
target_compile_options(bgl_metal PRIVATE -fobjc-arc)

target_link_libraries(bgl_metal
    PUBLIC core assetlib_structs glm::glm slang::slang
    PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore"
)
