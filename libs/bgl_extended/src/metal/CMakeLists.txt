# metal-cpp is Apple's C++ interface to Metal -- header-only, not on vcpkg, so pulled in here.
# Pinned to a commit on the mirror's metal-cpp_27 branch (the macOS 26 / current SDK line).
include(FetchContent)
FetchContent_Declare(metalcpp
    GIT_REPOSITORY https://github.com/bkaradzic/metal-cpp.git
    GIT_TAG        c9727bc9468a90d7ea8fc89d5ee03b8d8992a570
)
FetchContent_MakeAvailable(metalcpp)

# bgl_extended_tests is a sibling directory, so it cannot see metalcpp_SOURCE_DIR: a Metal-only case there
# needs the headers this pulled in.
set(BGL_METALCPP_INCLUDE_DIR "${metalcpp_SOURCE_DIR}" CACHE INTERNAL "metal-cpp headers")

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
        # The generated IDL headers are a build artifact, and a generated one includes its
        # hand-written imports as siblings -- so both roots, as bgl_extended_objects has them.
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/../idl
    SYSTEM PRIVATE ${metalcpp_SOURCE_DIR}
)

target_precompile_headers(bgl_metal
    PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

# metal-cpp emits its selector and class symbols in whichever TU defines the *_PRIVATE_IMPLEMENTATION
# macros before including the headers. A PCH would include them first and the definitions would be
# skipped, so this one file compiles without it.
set_source_files_properties(MetalImpl.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_compile_definitions(bgl_metal PRIVATE RENDERER_BACKEND_METAL)

target_link_libraries(bgl_metal
    PUBLIC bgl_intfc core assetlib_structs glm::glm slang::slang
    PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore"
)
