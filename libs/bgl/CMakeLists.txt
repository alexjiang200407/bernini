# bgl, the renderer. include/bgl is its public surface -- what a game compiles against, <bgl/...>,
# bgl::, BGL_API -- and src/ its implementation, with a backend per binary under src/d3d12,
# src/metal and src/vulkan. The RHI and the device beneath it are bgpu's (libs/bgpu).

file(GLOB_RECURSE BGL_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.hpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/bgl" PREFIX "Header Files"
	FILES ${BGL_HEADERS})

# The public surface's usage requirements and nothing else: what the backends' object libraries and
# the selfcheck compile against without linking the renderer they are part of, and what an SDK
# gamelib -- which links the renderer's file, not its target (libs/gamelib/CMakeLists.txt) -- passes
# on to its clients.
add_library(bgl_headers INTERFACE)

target_include_directories(bgl_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The entire closure: core carries Ref/SharedRef/slot_handle, assetlib_structs the decoded PODs, bgpu
# the GPU context CreateGraphics takes. glm is reached through <core/glm.h>, and core links glm::glm
# PUBLIC, so it needs no entry of its own. bgpu links core and Slang only, never back up here.
target_link_libraries(bgl_headers INTERFACE core assetlib_structs bgpu)

# Here rather than on bgl: an SDK gamelib never sees bgl's own usage requirements.
if (BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "SHARED")
	target_compile_definitions(bgl_headers INTERFACE BGL_SHARED)
endif()

# Every public header compiled against the public surface alone, so one that reaches into src/ stops
# the build -- see src/selfcheck.cpp for why it is a library and not an executable.
add_library(bgl_selfcheck STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp
	${BGL_HEADERS}
)

target_precompile_headers(bgl_selfcheck PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

# PRIVATE, and the surface only: linking the renderer would defeat the check.
target_link_libraries(bgl_selfcheck PRIVATE bgl_headers)

enable_strict_compiler(bgl_selfcheck)

# The contract's Slang half: what a game surface conforms to and reads through, and nothing that
# names a handle, an arena or a bucket -- the rule docs/bgl_api.md holds headers to, held for shaders.
# Its own root, shaders/include, as the C++ half has include/: staged into the one ./shaders/src every
# owner resolves from, owning bgl/ there, after bgpu's tree and before the renderer's so the root's
# make_directory never races.
set(BGL_SHADER_CONTRACT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include")
file(GLOB_RECURSE BGL_SHADER_CONTRACT_SOURCES CONFIGURE_DEPENDS
	${BGL_SHADER_CONTRACT_ROOT}/*.slang
)

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
add_dependencies(bgl_copy_contract_shaders bgpu_copy_shaders)

# Membership: every module here is compiled with only this tree on the search path, so an import of
# anything else -- the renderer's tree included -- fails to resolve. A game runs the same check on its own modules with this tree as the one path.
#
# The port stages slangc beside slangd under vcpkg's tools/, which is not on PATH; the hint is what
# finds it on a preset whose renderer never runs a build-time shader step of its own. This is the
# first find_program of slangc in the tree, so it fills the cache entry the D3D12 shader step's own
# find_program of the same name then reads back.
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

# The IDL first: bgl_idl_generate must exist before anything below can depend on it. Its Slang
# modules live under shaders/src/idl with the rest of the renderer's tree; it generates only C++.
add_subdirectory(idl)

# The renderer: every source under src/ but the backends' and the selfcheck, compiled once into objects
# the library and the suite both take.
file(GLOB_RECURSE BGL_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
# src/swapchain is the target every backend that presents through a Swapchain shares; such a
# backend compiles it, and Metal, whose target is its own, does not.
list(FILTER BGL_SOURCES EXCLUDE REGEX ".*/src/(d3d12|metal|vulkan|swapchain)/.*")
list(REMOVE_ITEM BGL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp)

file(GLOB_RECURSE BGL_PRIVATE_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)
list(FILTER BGL_PRIVATE_HEADERS EXCLUDE REGEX ".*/src/(d3d12|metal|vulkan|swapchain)/.*")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${BGL_SOURCES} ${BGL_PRIVATE_HEADERS})

add_library(bgl_objects OBJECT
	${BGL_SOURCES}
	${BGL_PRIVATE_HEADERS}
)

target_include_directories(bgl_objects
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
)


target_compile_definitions(bgl_objects PRIVATE
	BGL_EXPORTS
)

target_precompile_headers(bgl_objects
	PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

find_package(slang CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)  # stb_image_write, for RenderContext's screenshot PNG encode

target_include_directories(bgl_objects PRIVATE ${Stb_INCLUDE_DIR})

target_link_libraries(bgl_objects
	PUBLIC
		bgl_headers
		bgl_idl
		bgpu
		core
		assetlib_structs
		slang::slang
		glm::glm
)

if(RENDERER_BACKEND STREQUAL "DX12")
	add_subdirectory(src/d3d12)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	add_subdirectory(src/metal)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	add_subdirectory(src/vulkan)
endif()

add_dependencies(bgl_objects bgl_idl_generate)
if(RENDERER_BACKEND STREQUAL "DX12")
	add_dependencies(bgl_d3d12 bgl_idl_generate)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	add_dependencies(bgl_metal bgl_idl_generate)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	add_dependencies(bgl_vulkan bgl_idl_generate)
endif()

enable_strict_compiler(bgl_objects)
enable_coverage(bgl_objects)

# CreateGraphics is only ever defined inside a backend, and the shader step compiles for the
# backend's IR, so nothing below can be built without one.
if(NOT BERNINI_HAS_RENDERER)
	message(STATUS
		"RENDERER_BACKEND=${RENDERER_BACKEND} - bgl_objects only (no bgl runtime, shaders or tests)")
	return()
endif()

# STATIC unless the build asks for the renderer as a DLL (the root CMakeLists.txt decides, for
# core_process too). Either way it is the same objects; only the last step differs.
if(RENDERER_BACKEND STREQUAL "DX12")
	add_library(bgl ${BERNINI_RENDERER_LIBRARY_TYPE}
		$<TARGET_OBJECTS:bgl_objects>
		$<TARGET_OBJECTS:bgl_d3d12>
		${BGL_HEADERS}
	)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	add_library(bgl ${BERNINI_RENDERER_LIBRARY_TYPE}
		$<TARGET_OBJECTS:bgl_objects>
		$<TARGET_OBJECTS:bgl_metal>
		${BGL_HEADERS}
	)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	add_library(bgl ${BERNINI_RENDERER_LIBRARY_TYPE}
		$<TARGET_OBJECTS:bgl_objects>
		$<TARGET_OBJECTS:bgl_vulkan>
		${BGL_HEADERS}
	)
else()
	message(FATAL_ERROR
		"RENDERER_BACKEND must be DX12, METAL, VULKAN or NONE (got '${RENDERER_BACKEND}')")
endif()

target_include_directories(bgl
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_dependencies(bgl bgl_idl_generate)

target_link_libraries(bgl PUBLIC bgl_objects glm::glm)

enable_strict_compiler(bgl)

target_compile_definitions(bgl PRIVATE
	BGL_EXPORTS
)

# The build-time Slang step emits DXIL; only the D3D12 backend consumes it. The Metal backend
# compiles its shaders at runtime (MSL via newLibraryWithSource) and needs no build-time step.
if(RENDERER_BACKEND STREQUAL "DX12")
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/shaders)
	add_dependencies(bgl_copy_shader_src bgl_copy_contract_shaders)
	add_dependencies(bgl bgl_copy_shaders bgl_copy_shader_src)

	target_compile_definitions(bgl_d3d12 PRIVATE
		BGL_EXPORTS
	)

	target_compile_definitions(bgl PRIVATE RENDERER_BACKEND_DX12)
	target_link_libraries(bgl PRIVATE bgl_d3d12)

	enable_strict_compiler(bgl_d3d12)
	enable_coverage(bgl_d3d12)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	target_compile_definitions(bgl_metal PRIVATE BGL_EXPORTS)
	# bgl_objects too, not just the backend: DescriptorHandle's alignment is per-backend, and
	# bgl_objects is what compiles the scene code that puts one in a GPU struct.
	target_compile_definitions(bgl PRIVATE RENDERER_BACKEND_METAL)
	target_compile_definitions(bgl_objects PRIVATE RENDERER_BACKEND_METAL)
	# bgl embeds bgl_metal's objects, so it links the frameworks bgl_metal's PRIVATE deps do not carry.
	target_link_libraries(bgl PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore")
	enable_strict_compiler(bgl_metal)
	enable_coverage(bgl_metal)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	target_compile_definitions(bgl_vulkan PRIVATE BGL_EXPORTS)
	target_compile_definitions(bgl PRIVATE RENDERER_BACKEND_VULKAN)
	enable_strict_compiler(bgl_vulkan)
	enable_coverage(bgl_vulkan)
endif()

# Metal and Vulkan compile Slang at runtime, so the raw .slang sources must sit beside the
# executable -- the device's session resolves modules from "./shaders/src", and a build that
# staged none aborts on the first program-cache miss with "cannot open file". Hung off bgl, the
# way the D3D12 build hangs bgl_copy_shader_src off it, so every consumer of a device gets them.
#
# A target of its own rather than a POST_BUILD step: POST_BUILD only runs when the library
# relinks, so editing a .slang alone would leave the staged copy stale.
if(RENDERER_BACKEND MATCHES "^(METAL|VULKAN)$")
	file(GLOB_RECURSE BGL_STAGED_SHADER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.slang
		${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.bin
	)

	set(BGL_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/staged_shaders.stamp")
	add_custom_command(
		OUTPUT  "${BGL_SHADER_STAMP}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/src"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
		COMMAND ${CMAKE_COMMAND} -E touch "${BGL_SHADER_STAMP}"
		DEPENDS ${BGL_STAGED_SHADER_SOURCES}
		COMMENT "Staging Slang sources for runtime compilation"
		VERBATIM
	)
	add_custom_target(bgl_stage_shaders ALL DEPENDS "${BGL_SHADER_STAMP}")
	add_dependencies(bgl_stage_shaders bgl_copy_contract_shaders)
	add_dependencies(bgl bgl_stage_shaders)
endif()

# The suite's sources are backend-agnostic (RHI interfaces + CreateGraphics), so it builds on either
# backend from one glob. What differs is the backend objects it embeds.
if (BUILD_TESTS AND RENDERER_BACKEND STREQUAL "DX12")
    find_package(Catch2 CONFIG REQUIRED)
	find_package(Stb REQUIRED)  # stb_image for golden-image comparison

    file(GLOB_RECURSE BGL_TESTS_SOURCE CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )
    
    add_executable(bgl_tests
        ${BGL_TESTS_SOURCE}
        $<TARGET_OBJECTS:bgl_objects>
    )
    
    target_include_directories(bgl_tests
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
            ${Stb_INCLUDE_DIR}
            # gamelib's header-only LOD math, which LodSelect_test holds the GPU's choice against.
            ${BERNINI_ROOT}/libs/gamelib/include
    )

    target_force_include(bgl_tests
        PRIVATE "${BERNINI_ROOT}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
    )
    
    target_link_libraries(
			bgl_tests
		PRIVATE
			bgl_objects
			assetlib
			Catch2::Catch2
			Catch2::Catch2WithMain
			core
	)

	# The DLLs a compile loads are bgpu's to stage, which the suite links through bgl_objects.
	add_dependencies(bgl_tests bgl_copy_shaders bgl_copy_shader_src bgl_copy_shader_tests bgl_idl_generate)

	target_precompile_headers(bgl_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${BGL_TESTS_SOURCE})

    if(RENDERER_BACKEND STREQUAL "DX12")
        target_compile_definitions(bgl_tests PRIVATE RENDERER_BACKEND_DX12)
		target_link_libraries(bgl_tests PRIVATE bgl_d3d12 bgpu_d3d12_agility)
		target_sources(bgl_tests PRIVATE $<TARGET_OBJECTS:bgl_d3d12>)
    endif()

	target_compile_definitions(bgl_tests PRIVATE
		BGL_EXPORTS
	)

	set_target_properties(bgl_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	# Deploy the shared asset directory next to the test exe (cwd at run time), so
	# tests resolve maps/meshes as "assets/Data/<name>" and golden refs as
	# "assets/golden/<name>.png" -- the goldens are test fixtures, not project assets, so they sit
	# beside the project rather than inside it. copy_assets (root CMakeLists.txt) stages it once
	# for every consumer.
	add_dependencies(bgl_tests copy_assets)

	enable_strict_compiler(bgl_tests)
	enable_coverage(bgl_tests)
endif()

# Metal and Vulkan stage their test shaders as their sources are, for runtime compilation.
if (BUILD_TESTS AND RENDERER_BACKEND MATCHES "^(METAL|VULKAN)$")
	find_package(Catch2 CONFIG REQUIRED)
	find_package(Stb REQUIRED)  # stb_image for golden-image comparison

	if(RENDERER_BACKEND STREQUAL "METAL")
		set(BGL_TESTS_BACKEND bgl_metal)
		set(BGL_TESTS_BACKEND_DIR metal)
	else()
		set(BGL_TESTS_BACKEND bgl_vulkan)
		set(BGL_TESTS_BACKEND_DIR vulkan)
	endif()

	file(GLOB_RECURSE BGL_TESTS_SOURCE CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(bgl_tests
		${BGL_TESTS_SOURCE}
		$<TARGET_OBJECTS:bgl_objects>
		$<TARGET_OBJECTS:${BGL_TESTS_BACKEND}>
	)

	target_include_directories(bgl_tests
		PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/src
			# The backend's own root: its headers include each other unprefixed, so a backend-only
			# case cannot reach one without it.
			${CMAKE_CURRENT_SOURCE_DIR}/src/${BGL_TESTS_BACKEND_DIR}
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
			${Stb_INCLUDE_DIR}
			# gamelib's header-only LOD math, which LodSelect_test holds the GPU's choice against.
			${BERNINI_ROOT}/libs/gamelib/include
	)

	target_force_include(bgl_tests
		PRIVATE "${BERNINI_ROOT}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
	)

	target_link_libraries(bgl_tests
		PRIVATE
			bgl_objects
			${BGL_TESTS_BACKEND}
			assetlib
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)
	# Embeds bgl_metal's objects, so it links the frameworks bgl_metal's PRIVATE deps do not carry.
	if(RENDERER_BACKEND STREQUAL "METAL")
		target_link_libraries(bgl_tests
			PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore")
	endif()

	add_dependencies(bgl_tests bgl_idl_generate copy_assets)

	# Only the test shaders: the src tree beside them is staged by bgl_stage_shaders, which the
	# suite depends on as well because it links the backend's objects rather than bgl. One writer per
	# directory -- two targets copying into the same one can run at once.
	file(GLOB_RECURSE BGL_STAGED_TEST_SHADER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests/*.slang
	)

	set(BGL_TEST_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/staged_test_shaders.stamp")
	add_custom_command(
		OUTPUT  "${BGL_TEST_SHADER_STAMP}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/tests"
		COMMAND ${CMAKE_COMMAND} -E touch "${BGL_TEST_SHADER_STAMP}"
		DEPENDS ${BGL_STAGED_TEST_SHADER_SOURCES}
		COMMENT "Staging Slang test shaders for runtime compilation"
		VERBATIM
	)
	add_custom_target(bgl_stage_test_shaders DEPENDS "${BGL_TEST_SHADER_STAMP}")
	add_dependencies(bgl_tests bgl_stage_shaders bgl_stage_test_shaders)

	target_precompile_headers(bgl_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	target_compile_definitions(bgl_tests PRIVATE RENDERER_BACKEND_${RENDERER_BACKEND} BGL_EXPORTS)

	enable_strict_compiler(bgl_tests)
	enable_coverage(bgl_tests)
endif()
