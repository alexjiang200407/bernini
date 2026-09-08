cmake_minimum_required(VERSION 3.24)

# The smallest possible consumer of the engine: a project that is the top of its own build, adds
# bernini as a subdirectory, and compiles one translation unit against the public headers.
#
# It exists because nothing else here is one. Every other build in the repository is the engine
# building itself, where a path built from CMAKE_SOURCE_DIR is correct and stays correct through
# any refactor -- so the property this file tests is invisible to the whole suite without it.
#
# Everything below is what a consumer owes the engine, and nothing else. If this file has to grow,
# docs/embedding.md is out of date. `just embed` configures and builds it.

if (NOT DEFINED BERNINI_DIR OR BERNINI_DIR STREQUAL "")
    set(BERNINI_DIR "$ENV{BERNINI_DIR}")
endif()
if (NOT EXISTS "${BERNINI_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "BERNINI_DIR must name a bernini checkout (got '${BERNINI_DIR}')")
endif()
get_filename_component(BERNINI_DIR "${BERNINI_DIR}" ABSOLUTE)

# vcpkg runs from its toolchain during project() below and reads exactly one manifest. Ours is the
# engine's, which also gets its builtin-baseline and -- since vcpkg resolves the relative
# overlay-ports path against the configuration file's own directory -- its cmake/ports overlay.
set(VCPKG_MANIFEST_DIR "${BERNINI_DIR}")

# The engine stages its shaders and assets here and refuses to configure if nobody has chosen one,
# because a consumer whose executable lands elsewhere gets a build that succeeds and a binary that
# cannot find its shaders.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

project(bernini_embed CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${BERNINI_DIR} ${CMAKE_BINARY_DIR}/bernini)

add_executable(bernini_embed main.cpp)

target_link_libraries(bernini_embed PRIVATE core bgl_extended gamelib assetlib)

# Part of the surface: the engine compiles its own headers behind this, and a consumer that does
# not use it compiles them differently to every translation unit inside the engine.
target_precompile_headers(bernini_embed PRIVATE ${BERNINI_DIR}/PCH/pch.h)
