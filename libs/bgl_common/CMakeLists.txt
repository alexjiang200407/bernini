# The substrate the renderers share: what is neither the contract in bgl nor specific to one
# renderer's binding model. bgl_extended links it today; the second renderer ROADMAP.md:381-390
# commits to is what it exists for.
#
# Nothing here is part of the engine's public surface, so there is no BGL_API and no BGL_EXPORTS:
# the archive links into each renderer and its symbols stop there. Membership is a property of a
# file, not a judgement -- a header belongs here only if it names no binding type and reaches
# nothing outside this closure, which is what bgl_common_selfcheck below enforces.

# The IDL first: bgl_idl_generate must exist before anything below can depend on it.
add_subdirectory(idl)

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

target_include_directories(bgl_common PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	# <bgl_common/idl/<Module>.h>: the generated mirrors land under the same prefix as the hand-written
	# offset primitives, so a consumer spells both the same way and cannot tell which is which.
	${CMAKE_BINARY_DIR}/generated
	# A generated header includes its imports as siblings, and Entry/Range/RangeWithCount/RawEntry
	# are hand-written, so their directory has to be reachable bare.
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl_common/idl
)

add_dependencies(bgl_common bgl_idl_generate)

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

# Its one TU includes the generated headers, and a custom-target dependency does not travel through
# target_link_libraries: without this its compile is unordered against codegen on a clean build.
add_dependencies(bgl_common_selfcheck bgl_idl_generate)

enable_strict_compiler(bgl_common_selfcheck)

# The shared Slang tree, staged where every renderer's session resolves modules from
# (`./shaders/src`, see docs/rhi.md). A module's import name is its path under that root, so the
# layout here is the layout there and `import idl.Submesh` needs no renderer to say where it lives.
# A renderer's own copier writes the same root; the two stay disjoint because this one owns idl/
# and a renderer owns nothing under it, and the renderer's copier is ordered after this one so the
# shared root's make_directory never races.
#
# A target rather than a POST_BUILD step: POST_BUILD only runs when a library relinks, so editing
# a .slang alone would leave the staged copy stale.
file(GLOB_RECURSE BGL_COMMON_SHADER_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.slang
)

set(BGL_COMMON_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shaders.stamp")
add_custom_command(
	OUTPUT  "${BGL_COMMON_SHADER_STAMP}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/shaders/src"
			"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
	COMMAND ${CMAKE_COMMAND} -E touch "${BGL_COMMON_SHADER_STAMP}"
	DEPENDS ${BGL_COMMON_SHADER_SOURCES} bgl_idl_generate
	COMMENT "Staging the shared Slang sources"
	VERBATIM
)
add_custom_target(bgl_common_copy_shaders ALL DEPENDS "${BGL_COMMON_SHADER_STAMP}")
