cmake_minimum_required(VERSION 3.22)
project(BerniniEngine)

# Group targets into folders for IDE generators (Visual Studio / Xcode).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(${CMAKE_SOURCE_DIR}/cmake/target_force_include.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/copy_to_target.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compile_shader.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/enable_strict_compiler.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/assign_folder.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions($<$<CONFIG:Debug>:BERNINI_GPU_DEBUG>)


if(CMAKE_MAKE_PROGRAM MATCHES "msbuild|MSBUILD")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(libs/core)
set(FG_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/bgl)
add_subdirectory(libs/assetlib)

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
if (Qt6_FOUND)
    add_subdirectory(apps/editor)
else()
    message(STATUS "Qt6 not found - skipping editor (set CMAKE_PREFIX_PATH to your Qt install to enable it)")
endif()

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

get_property(EXAMPLE_TARGETS GLOBAL PROPERTY GLOBAL_EXAMPLE_TARGETS)

assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/core"     "misc")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/bgl"      "gfx")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib" "editor")

if (BUILD_EXAMPLES)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/examples" "examples")
endif()
if (Qt6_FOUND)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/apps/editor" "editor")
endif()
