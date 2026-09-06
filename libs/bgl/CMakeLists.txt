# bgl's public surface: the contract a renderer implements, owned by no renderer.
#
# bgl names the contract and nothing else, which is what every client source file already spells --
# <bgl/...>, bgl::, BGL_API. A renderer implementing it is named for what it is built on and lives
# elsewhere: bgl_extended for the bindless, mesh-shader tier.

file(GLOB_RECURSE BGL_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.hpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/bgl" PREFIX "Header Files"
	FILES ${BGL_HEADERS})

add_library(bgl INTERFACE)

target_include_directories(bgl INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The entire closure: core carries Ref/SharedRef/slot_handle, assetlib_structs the decoded PODs. glm
# is reached through <core/glm.h>, and core links glm::glm PUBLIC, so it needs no entry of its own.
target_link_libraries(bgl INTERFACE core assetlib_structs)

# An INTERFACE target compiles nothing, so on its own it enforces nothing. This one TU is the
# enforcement -- see src/selfcheck.cpp for why it is a library and not an executable.
add_library(bgl_selfcheck STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp
	${BGL_HEADERS}
)

target_precompile_headers(bgl_selfcheck PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

# PRIVATE, and bgl only: linking anything else would defeat the check.
target_link_libraries(bgl_selfcheck PRIVATE bgl)

enable_strict_compiler(bgl_selfcheck)

# The contract's own suite. Most of bgl is pure declaration, so what there is to run is the handful
# of client-side helpers it defines over its own types -- and they need no device, which is why they
# are testable here at all.
if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE BGL_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(bgl_tests ${BGL_TESTS_SOURCES})

	target_include_directories(bgl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src)

	target_link_libraries(bgl_tests
		PRIVATE
			bgl
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(bgl_tests
		PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(bgl_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files"
		FILES ${BGL_TESTS_SOURCES})

	enable_strict_compiler(bgl_tests)
	enable_coverage(bgl_tests)
endif()

# The contract's Slang half: what a game surface conforms to and reads through, and nothing that
# names a handle, an arena or a bucket -- the rule docs/bgl_api.md holds headers to, held for shaders.
# Staged into the one ./shaders/src every renderer resolves from, owning bgl/ there; the shared tree's
# copier is ordered after this one so the root's make_directory never races.
set(BGL_SHADER_CONTRACT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src")
file(GLOB_RECURSE BGL_SHADER_CONTRACT_SOURCES CONFIGURE_DEPENDS
	${BGL_SHADER_CONTRACT_ROOT}/*.slang
)
# The trees above this one compile against the contract, so both are theirs to read.
set(BGL_SHADER_CONTRACT_ROOT "${BGL_SHADER_CONTRACT_ROOT}" PARENT_SCOPE)
set(BGL_SHADER_CONTRACT_SOURCES "${BGL_SHADER_CONTRACT_SOURCES}" PARENT_SCOPE)

set(BGL_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shaders.stamp")
add_custom_command(
	OUTPUT  "${BGL_SHADER_STAMP}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${BGL_SHADER_CONTRACT_ROOT}"
			"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
	COMMAND ${CMAKE_COMMAND} -E touch "${BGL_SHADER_STAMP}"
	DEPENDS ${BGL_SHADER_CONTRACT_SOURCES}
	COMMENT "Staging the shader contract"
	VERBATIM
)
add_custom_target(bgl_copy_contract_shaders ALL DEPENDS "${BGL_SHADER_STAMP}")

# Membership, enforced the way bgl_common_check_shaders enforces the shared tree's: every module
# here is compiled with only this tree on the search path, so an import of anything else fails to
# resolve. A game runs the same check on its own modules with this tree as the one path.
#
# The port stages slangc beside slangd under vcpkg's tools/, which is not on PATH; the hint is what
# finds it on a preset whose renderer never runs a build-time shader step of its own. This is the
# first find_program of slangc in the tree, so it fills the cache entry the shared tree's checks and
# the D3D12 shader step's own find_program of the same name then read back.
#
# slangc by name, never slang: the port stages a `slang` beside it that is a different CLI, and on
# Windows nothing but the name tells the two apart. Every flag the checks and the DXIL step pass
# (-profile, -target, -entry, -stage, -o) is slangc's.
find_program(SLANG_EXECUTABLE
	NAMES slangc slangc.exe
	HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/shader-slang"
	DOC "Slang compiler, for the shader-contract membership check"
)
if(NOT SLANG_EXECUTABLE)
	message(FATAL_ERROR "slangc not found: bgl's shader contract cannot be checked.")
endif()

set(BGL_SHADER_CONTRACT_CHECKS "")
foreach(MODULE ${BGL_SHADER_CONTRACT_SOURCES})
	file(RELATIVE_PATH MODULE_REL "${BGL_SHADER_CONTRACT_ROOT}" "${MODULE}")
	set(MODULE_OUT "${CMAKE_CURRENT_BINARY_DIR}/shader_check/${MODULE_REL}.slang-module")
	get_filename_component(MODULE_OUT_DIR "${MODULE_OUT}" DIRECTORY)
	add_custom_command(
		OUTPUT  "${MODULE_OUT}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MODULE_OUT_DIR}"
		COMMAND ${SLANG_EXECUTABLE} "${MODULE}" -I "${BGL_SHADER_CONTRACT_ROOT}" -o "${MODULE_OUT}"
		DEPENDS ${BGL_SHADER_CONTRACT_SOURCES}
		COMMENT "shader-contract check ${MODULE_REL}"
		VERBATIM
	)
	list(APPEND BGL_SHADER_CONTRACT_CHECKS "${MODULE_OUT}")
endforeach()
add_custom_target(bgl_check_shaders ALL DEPENDS ${BGL_SHADER_CONTRACT_CHECKS})
