# The substrate the renderers share: what is neither the contract in bgl nor specific to one
# renderer's binding model. bgl_extended links it today; the second renderer ROADMAP.md:381-390
# commits to is what it exists for.
#
# Nothing here is part of the engine's public surface, so there is no BGL_API and no BGL_EXPORTS:
# the archive links into each renderer and its symbols stop there. Membership is a property of a
# file, not a judgement -- a header belongs here only if it names no binding type and reaches
# nothing outside this closure, which is what bgl_common_selfcheck below enforces.

file(GLOB_RECURSE BGL_COMMON_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(REMOVE_ITEM BGL_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp)

file(GLOB_RECURSE BGL_COMMON_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl_common/*.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files"
	FILES ${BGL_COMMON_SOURCES} ${BGL_COMMON_HEADERS})

find_package(slang CONFIG REQUIRED)

add_library(bgl_common STATIC
	${BGL_COMMON_SOURCES}
	${BGL_COMMON_HEADERS}
)

target_include_directories(bgl_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(bgl_common PUBLIC bgl core slang::slang)

# A renderer is a shared library and this archive is linked into one.
set_target_properties(bgl_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_precompile_headers(bgl_common PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h)

enable_strict_compiler(bgl_common)
enable_coverage(bgl_common)

# What this translation unit does NOT link is the point -- see src/selfcheck.cpp.
add_library(bgl_common_selfcheck STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp
	${BGL_COMMON_HEADERS}
)

target_precompile_headers(bgl_common_selfcheck PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h)

# PRIVATE, and bgl_common only: linking a renderer would defeat the check.
target_link_libraries(bgl_common_selfcheck PRIVATE bgl_common)

enable_strict_compiler(bgl_common_selfcheck)
