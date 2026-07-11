file(GLOB_RECURSE EXAMPLE_UTIL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE EXAMPLE_UTIL_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(example_util STATIC ${EXAMPLE_UTIL_SOURCES} ${EXAMPLE_UTIL_HEADERS})

find_package(SDL3 CONFIG REQUIRED)

# Shared demo scaffolding for the examples: an SDL3 window (DemoWindow) and a
# fly-camera input helper (FlyCamera). SDL3 is exposed PUBLIC so it lands next to
# each example exe via vcpkg's applocal deployment.
target_link_libraries(example_util
    PUBLIC
        core
        bgl
        SDL3::SDL3
)

target_include_directories(example_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_precompile_headers(example_util
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files"
    FILES ${EXAMPLE_UTIL_SOURCES} ${EXAMPLE_UTIL_HEADERS})
