# The GPU layer, owned by no renderer: the process's device and the compiler for it, which the
# application creates and hands to every library that runs work on the device, and the RHI each of
# them builds its own queues, resource managers and pipelines from -- bgl, and a client that
# runs its own compute beside the frame. It names every backend, and it holds process-wide state, so
# it is built as the renderer is (docs/core_process.md, "Linkage").
file(GLOB_RECURSE BGPU_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(FILTER BGPU_SOURCES EXCLUDE REGEX ".*/src/(d3d12|metal|vulkan)/.*")

file(GLOB_RECURSE BGPU_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgpu/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
list(FILTER BGPU_HEADERS EXCLUDE REGEX ".*/src/(d3d12|metal|vulkan)/.*")

# The backend's half: the device creation, the RHI's implementation, and on Metal the one
# translation unit that emits metal-cpp's symbols for the whole process. Without a backend only the
# compiler and the RHI's neutral half are built -- bgl_objects still compiles against them
# -- and CreateGpuContext and CreateDevice have no definition. Vulkan's half is the context alone:
# CreateGpuContext is defined and CreateDevice is not.
set(BGPU_BACKEND_DIR "")
set(BGPU_BACKEND_HAS_RHI OFF)
if(RENDERER_BACKEND STREQUAL "DX12")
	set(BGPU_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12")
	set(BGPU_BACKEND_HAS_RHI ON)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	set(BGPU_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/metal")
	set(BGPU_BACKEND_HAS_RHI ON)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	set(BGPU_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan")
endif()
if(BGPU_BACKEND_DIR)
	file(GLOB_RECURSE BGPU_BACKEND_SOURCES CONFIGURE_DEPENDS ${BGPU_BACKEND_DIR}/*.cpp)
	# The Agility SDK's exports are the executable's, never this library's: bgpu_d3d12_agility below.
	list(FILTER BGPU_BACKEND_SOURCES EXCLUDE REGEX ".*/agility/.*")
	file(GLOB_RECURSE BGPU_BACKEND_HEADERS CONFIGURE_DEPENDS ${BGPU_BACKEND_DIR}/*.h)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files"
	FILES ${BGPU_SOURCES} ${BGPU_HEADERS}
	      ${BGPU_BACKEND_SOURCES} ${BGPU_BACKEND_HEADERS})

find_package(slang CONFIG REQUIRED)

add_library(bgpu ${BERNINI_RENDERER_LIBRARY_TYPE}
	${BGPU_SOURCES}
	${BGPU_HEADERS}
	${BGPU_BACKEND_SOURCES}
	${BGPU_BACKEND_HEADERS}
)

if (BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "SHARED")
	target_compile_definitions(bgpu
		PUBLIC BGPU_SHARED
		PRIVATE BGPU_EXPORTS)
endif()

target_include_directories(bgpu
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
		# A generated IDL mirror includes the offset primitives it names as siblings ("Entry.h"), so
		# their directory has to be reachable bare.
		${CMAKE_CURRENT_SOURCE_DIR}/include/bgpu/idl
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${BGPU_BACKEND_DIR}
)

find_package(glm CONFIG REQUIRED)

target_link_libraries(bgpu PUBLIC core slang::slang glm::glm)

# DescriptorHandle's alignment is per backend and it sits in GPU structs every owner lays out, so
# every consumer of the RHI has to agree on it.
if(RENDERER_BACKEND STREQUAL "DX12")
	target_compile_definitions(bgpu PUBLIC RENDERER_BACKEND_DX12)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	target_compile_definitions(bgpu PUBLIC RENDERER_BACKEND_METAL)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	target_compile_definitions(bgpu PUBLIC RENDERER_BACKEND_VULKAN)
endif()

if(BGPU_BACKEND_DIR)
	target_precompile_headers(bgpu
		PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h ${BGPU_BACKEND_DIR}/pch.h
	)
else()
	target_precompile_headers(bgpu
		PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
	)
endif()

if(RENDERER_BACKEND STREQUAL "DX12")
	find_package(directx-headers CONFIG REQUIRED)
	find_package(winpixevent CONFIG REQUIRED)
	# PUBLIC: what a caller does with a native pointer bgpu hands out -- the renderer's swapchain --
	# needs these headers, though bgpu's own public headers only forward-declare the interfaces.
	target_link_libraries(bgpu
		PUBLIC  Microsoft::DirectX-Headers
		PRIVATE d3d12 dxgi dxguid Microsoft::WinPixEventRuntime)
	target_compile_definitions(bgpu PRIVATE $<$<CONFIG:Debug>:USE_PIX>)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	# metal-cpp is Apple's C++ interface to Metal -- header-only, not on vcpkg, so pulled in here.
	# Pinned to a commit on the mirror's metal-cpp_27 branch (the macOS 26 / current SDK line).
	# PUBLIC: every Metal user in the process -- the renderer's backend, a compute client, a
	# Metal-only test -- includes the same headers this one emitted the symbols for.
	include(FetchContent)
	FetchContent_Declare(metalcpp
		GIT_REPOSITORY https://github.com/bkaradzic/metal-cpp.git
		GIT_TAG        c9727bc9468a90d7ea8fc89d5ee03b8d8992a570
	)
	FetchContent_MakeAvailable(metalcpp)

	target_include_directories(bgpu SYSTEM PUBLIC ${metalcpp_SOURCE_DIR})
	target_link_libraries(bgpu PUBLIC "-framework Metal" "-framework Foundation" "-framework QuartzCore")

	# metal-cpp emits its selector and class symbols in whichever TU defines the
	# *_PRIVATE_IMPLEMENTATION macros before including the headers. A PCH would include them first
	# and the definitions would be skipped, so this one file compiles without it.
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/metal/MetalImpl.cpp
		PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	# volk loads the system's Vulkan loader at runtime and calls through function pointers of its
	# own, so nothing links vulkan-1 and a machine with no Vulkan starts and is refused in words
	# (SystemRequirements.h). Its header-only target: the pointers are defined once per process, by
	# src/vulkan/VolkImpl.cpp, which a PCH that includes the header first would guard out.
	find_package(volk CONFIG REQUIRED)
	find_package(VulkanUtilityLibraries CONFIG REQUIRED)
	target_link_libraries(bgpu PRIVATE volk::volk_headers Vulkan::UtilityHeaders)
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan/VolkImpl.cpp
		PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

enable_strict_compiler(bgpu)
enable_coverage(bgpu)

# What a D3D12 device and a shader compile load by name goes beside the executables -- here, because
# creating the device and compiling for it are bgpu's, so every owner of it (a renderer, a compute
# client, bgpu_tests) gets them by linking bgpu. The runtime directory is named rather than
# $<TARGET_FILE_DIR:bgpu>, which a static bgpu does not share with the executables.
if(RENDERER_BACKEND STREQUAL "DX12")
	# Slang reaches DXC through GetProcAddress -- dxcompiler.dll to emit DXIL, dxil.dll to sign it --
	# so nothing imports either, and vcpkg's applocal deployment stages a DLL by walking the import
	# table. The Slang release carries neither. Unsigned bytecode loads only in Developer Mode, so both
	# are copied by hand, as the Agility SDK's are. Found here, before anything that validates shaders
	# at build time with the same DXC; the port's config cannot be found twice.
	#
	# dxil.dll is taken from beside dxcompiler.dll rather than through Microsoft::DXIL, whose config
	# sets IMPORTED_LOCATION_RELASE -- so that target resolves to nothing in a Release build. Reading
	# both from one directory also keeps the compiler and its validator a matched pair, which they
	# have to be: a validator older than the compiler rejects the container.
	find_package(directx-dxc CONFIG REQUIRED)
	find_package(directx12-agility CONFIG REQUIRED)
	add_custom_command(TARGET bgpu POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:Microsoft::DirectXShaderCompiler>"
				"$<TARGET_FILE_DIR:Microsoft::DirectXShaderCompiler>/dxil.dll"
				"$<TARGET_FILE:Microsoft::DirectX12-Core>"
				"$<TARGET_FILE:Microsoft::DirectX12-Layers>"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
		COMMENT "Staging what the D3D12 device and the shader compiler load by name"
		VERBATIM)

	# The Agility SDK finds its DLLs through two symbols the *executable* exports, so they cannot live
	# in bgpu: every D3D12 executable links this OBJECT library itself.
	add_library(bgpu_d3d12_agility OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/agility/D3D12Agility.cpp)
	enable_strict_compiler(bgpu_d3d12_agility)
elseif(RENDERER_BACKEND STREQUAL "VULKAN")
	# The validation layer is found by the loader through a manifest, so nothing imports it either.
	# Staged beside the executables, where the context adds its directory to the loader's search
	# (GpuContext_vulkan.cpp), so the debug layer needs no Vulkan SDK on the machine. The port
	# exports no target, and the optimized build is taken in every configuration: the debug one
	# validates several times slower and shares no state with the binary that loads it.
	set(BGPU_VULKAN_LAYER_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
	set(BGPU_VULKAN_LAYER_FILES
		"${BGPU_VULKAN_LAYER_DIR}/VkLayer_khronos_validation.json"
		"${BGPU_VULKAN_LAYER_DIR}/VkLayer_khronos_validation.dll")
	if(EXISTS "${BGPU_VULKAN_LAYER_DIR}/VkLayer_khronos_validation.json")
		# A target of its own rather than a POST_BUILD step, which runs only when bgpu relinks: a
		# layer vcpkg rebuilt would leave the staged copy stale.
		set(BGPU_VULKAN_LAYER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/vulkan_layer.stamp")
		add_custom_command(
			OUTPUT  "${BGPU_VULKAN_LAYER_STAMP}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
					${BGPU_VULKAN_LAYER_FILES}
					"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
			COMMAND ${CMAKE_COMMAND} -E touch "${BGPU_VULKAN_LAYER_STAMP}"
			DEPENDS ${BGPU_VULKAN_LAYER_FILES}
			COMMENT "Staging the Vulkan validation layer"
			VERBATIM)
		add_custom_target(bgpu_stage_vulkan_layer ALL DEPENDS "${BGPU_VULKAN_LAYER_STAMP}")
		add_dependencies(bgpu bgpu_stage_vulkan_layer)
	else()
		message(STATUS
			"No Vulkan validation layer in ${BGPU_VULKAN_LAYER_DIR}: GpuContextDesc::enableDebugLayer "
			"needs one installed on the machine")
	endif()
endif()

# The RHI's Slang half: the offset primitives (idl.Entry, idl.Range ...), the buffer family that reads
# through them and the GPU assert channel they report to. Staged into the one ./shaders/src every
# owner's session resolves from, so an import names the same module in the renderer and beside it.
# First of the copiers: every other tree imports this one, and each is ordered after it so the root's
# make_directory never races.
set(BGPU_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src")
file(GLOB_RECURSE BGPU_SHADER_SOURCES CONFIGURE_DEPENDS ${BGPU_SHADER_ROOT}/*.slang)
set(BGPU_SHADER_ROOT "${BGPU_SHADER_ROOT}" PARENT_SCOPE)
set(BGPU_SHADER_SOURCES "${BGPU_SHADER_SOURCES}" PARENT_SCOPE)

set(BGPU_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shaders.stamp")
add_custom_command(
	OUTPUT  "${BGPU_SHADER_STAMP}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${BGPU_SHADER_ROOT}"
			"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
	COMMAND ${CMAKE_COMMAND} -E touch "${BGPU_SHADER_STAMP}"
	DEPENDS ${BGPU_SHADER_SOURCES}
	COMMENT "Staging bgpu's Slang sources"
	VERBATIM
)
add_custom_target(bgpu_copy_shaders ALL DEPENDS "${BGPU_SHADER_STAMP}")
add_dependencies(bgpu bgpu_copy_shaders)

# Membership: every module here compiles with only this tree on the search path, so an import that
# reaches into a renderer fails to resolve and stops the build -- what bgpu_selfcheck is for the
# headers. SLANG_EXECUTABLE is the cache entry bgl's contract check filled.
set(BGPU_SHADER_CHECKS "")
foreach(MODULE ${BGPU_SHADER_SOURCES})
	file(RELATIVE_PATH MODULE_REL "${BGPU_SHADER_ROOT}" "${MODULE}")
	set(MODULE_OUT "${CMAKE_CURRENT_BINARY_DIR}/shader_check/${MODULE_REL}.slang-module")
	get_filename_component(MODULE_OUT_DIR "${MODULE_OUT}" DIRECTORY)
	add_custom_command(
		OUTPUT  "${MODULE_OUT}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MODULE_OUT_DIR}"
		COMMAND ${SLANG_EXECUTABLE} "${MODULE}" -I "${BGPU_SHADER_ROOT}" -o "${MODULE_OUT}"
		DEPENDS ${BGPU_SHADER_SOURCES}
		COMMENT "bgpu module check ${MODULE_REL}"
		VERBATIM
	)
	list(APPEND BGPU_SHADER_CHECKS "${MODULE_OUT}")
endforeach()
add_custom_target(bgpu_check_shaders ALL DEPENDS ${BGPU_SHADER_CHECKS})

add_subdirectory(idl)

# The C++ mirrors of bgpu's IDL, generated per backend into the build tree as the renderer's are
# (libs/bgl/idl/CMakeLists.txt). Only the modules with a concrete
# layout: the offset primitives are generics, and their C++ halves are hand-written in include/bgpu/idl.
set(BGPU_IDL_ROOT "${BGPU_SHADER_ROOT}/idl")
set(BGPU_IDL_OUT_ROOT "${CMAKE_BINARY_DIR}/generated/bgpu_idl")
set(BGPU_IDL_CPP_SOURCES
	DebugRecord.slang
	ErrorCode.slang
	RawArena.slang
	RawRange.slang
	RecordHeader.slang
)
if(RENDERER_BACKEND STREQUAL "METAL")
	set(BGPU_IDL_LAYOUT_ARGS --metal-layout)
else()
	set(BGPU_IDL_LAYOUT_ARGS "")
endif()
set(BGPU_IDL_OUTPUTS "")
foreach(module ${BGPU_IDL_CPP_SOURCES})
	get_filename_component(name "${module}" NAME_WE)
	set(output "${BGPU_IDL_OUT_ROOT}/bgpu/idl/${name}.h")
	add_custom_command(
		OUTPUT  "${output}"
		COMMAND bgpu_idlgen
				--src-root "${BGPU_IDL_ROOT}"
				${BGPU_IDL_LAYOUT_ARGS}
				--cpp-out-dir "${BGPU_IDL_OUT_ROOT}/bgpu/idl"
				--namespace "bgpu::idl"
				-I "${BGPU_IDL_ROOT}"
				"${BGPU_IDL_ROOT}/${module}"
		DEPENDS bgpu_idlgen "${BGPU_IDL_ROOT}/${module}"
		COMMENT "idlgen bgpu ${module}"
		VERBATIM
	)
	list(APPEND BGPU_IDL_OUTPUTS "${output}")
endforeach()
add_custom_target(bgpu_idl_generate ALL DEPENDS ${BGPU_IDL_OUTPUTS})
add_dependencies(bgpu bgpu_idl_generate)
# The second root: a renderer's generated mirror includes the ones it embeds as siblings ("RawRange.h").
target_include_directories(bgpu PUBLIC ${BGPU_IDL_OUT_ROOT} ${BGPU_IDL_OUT_ROOT}/bgpu/idl)

# What this target does NOT link is the point: every public header compiles alone -- no PCH, and
# bgpu's own closure only -- so one that reaches into the renderer (bgl) or
# leans on an include it never wrote stops the build here, rather than in a compute client that
# includes it first. A header per translation unit, generated from the glob, so a new header is
# checked without anyone listing it.
file(GLOB_RECURSE BGPU_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/bgpu/*.h)
# The D3D12 ones compile empty off Windows; metal-cpp exists only on a Metal build.
if(NOT RENDERER_BACKEND STREQUAL "METAL")
	list(FILTER BGPU_PUBLIC_HEADERS EXCLUDE REGEX ".*/include/bgpu/metal/.*")
endif()
set(BGPU_SELFCHECK_SOURCES "")
foreach(header ${BGPU_PUBLIC_HEADERS})
	file(RELATIVE_PATH header_rel "${CMAKE_CURRENT_SOURCE_DIR}/include" "${header}")
	string(MAKE_C_IDENTIFIER "${header_rel}" check_name)
	set(check_source "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${check_name}.cpp")
	file(GENERATE OUTPUT ${check_source} CONTENT "#include <${header_rel}>\n")
	list(APPEND BGPU_SELFCHECK_SOURCES ${check_source})
endforeach()
add_library(bgpu_selfcheck STATIC ${BGPU_SELFCHECK_SOURCES})
# PRIVATE, and bgpu only: linking anything else would defeat the check.
target_link_libraries(bgpu_selfcheck PRIVATE bgpu)
set_target_properties(bgpu_selfcheck PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
add_dependencies(bgpu_selfcheck bgpu_idl_generate)
enable_strict_compiler(bgpu_selfcheck)

# The context half -- the device, the Slang sessions, the program cache -- never includes the RHI
# (docs/bgpu.md). bgpu_selfcheck cannot see that: it compiles against all of bgpu. This one stages only the
# context's headers into a tree of their own and compiles each against it, so an include of an RHI
# header fails to resolve. A new context header is added to the list; one left off is RHI by default.
set(BGPU_CONTEXT_HEADERS
	api.h
	GpuContext.h
	MemoryTag.h
	ProgramCache.h
	SlangErrorChecker.h
	SystemRequirements.h
	d3d12/D3d12ErrorChecker.h
)
if(RENDERER_BACKEND STREQUAL "METAL")
	list(APPEND BGPU_CONTEXT_HEADERS metal/MetalErrorChecker.h)
endif()
set(BGPU_CONTEXT_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/context_include")
set(BGPU_CONTEXT_SELFCHECK_SOURCES "")
foreach(header ${BGPU_CONTEXT_HEADERS})
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/include/bgpu/${header}"
		"${BGPU_CONTEXT_INCLUDE}/bgpu/${header}"
		COPYONLY)
	string(MAKE_C_IDENTIFIER "bgpu/${header}" check_name)
	set(check_source "${CMAKE_CURRENT_BINARY_DIR}/context_selfcheck/${check_name}.cpp")
	file(GENERATE OUTPUT ${check_source} CONTENT "#include <bgpu/${header}>\n")
	list(APPEND BGPU_CONTEXT_SELFCHECK_SOURCES ${check_source})
endforeach()
add_library(bgpu_context_selfcheck STATIC ${BGPU_CONTEXT_SELFCHECK_SOURCES})
# Not bgpu: its include directories are the ones this check exists to withhold.
target_include_directories(bgpu_context_selfcheck PRIVATE ${BGPU_CONTEXT_INCLUDE})
target_link_libraries(bgpu_context_selfcheck PRIVATE core slang::slang)
target_compile_definitions(bgpu_context_selfcheck
	PRIVATE $<TARGET_PROPERTY:bgpu,INTERFACE_COMPILE_DEFINITIONS>)
set_target_properties(bgpu_context_selfcheck PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
enable_strict_compiler(bgpu_context_selfcheck)

# The suite needs a device, so there is none without a backend.
if (BUILD_TESTS AND NOT RENDERER_BACKEND STREQUAL "NONE")
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE BGPU_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	# A backend with a context and no RHI builds the context's cases and its own backend's alone:
	# every other one reaches CreateDevice, which it does not define.
	if(NOT BGPU_BACKEND_HAS_RHI)
		list(FILTER BGPU_TESTS_SOURCES INCLUDE REGEX ".*/tests/src/(context|vulkan)/.*")
	endif()

	add_executable(bgpu_tests ${BGPU_TESTS_SOURCES})

	# The backend's own root and bgpu's too: a backend-only case reaches one of its classes, which
	# include each other and the neutral sources unprefixed.
	target_include_directories(bgpu_tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
			${BGPU_BACKEND_DIR}
			${CMAKE_CURRENT_SOURCE_DIR}/src
	)

	if(RENDERER_BACKEND STREQUAL "VULKAN")
		# A Vulkan-only case calls through the pointers bgpu's VolkImpl.cpp defines.
		target_link_libraries(bgpu_tests PRIVATE volk::volk_headers)
	endif()

	if(RENDERER_BACKEND STREQUAL "DX12")
		# The Agility SDK's two exports must be the executable's.
		target_link_libraries(bgpu_tests PRIVATE d3d12 bgpu_d3d12_agility)
		# A shared bgpu exports only what BGPU_API marks, and the allocator it tests directly is not
		# part of that surface, so the case compiles its own copy.
		if(BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "SHARED")
			target_sources(bgpu_tests PRIVATE ${BGPU_BACKEND_DIR}/resource/DescriptorAllocator_d3d12.cpp)
			target_include_directories(bgpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
			target_precompile_headers(bgpu_tests PRIVATE ${BGPU_BACKEND_DIR}/pch.h)
		endif()
	endif()

	target_link_libraries(bgpu_tests
		PRIVATE
			bgpu
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(bgpu_tests
		PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(bgpu_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	# The session cases load modules from ./shaders/src beside the suite, staged by bgpu exactly as a
	# renderer resolves them.
	add_dependencies(bgpu_tests bgpu_copy_shaders)

	# The suite's own kernels, under bgpu/ in the tests root the sessions search, which no other
	# copier writes.
	file(GLOB_RECURSE BGPU_TEST_SHADER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests/*.slang)
	set(BGPU_TEST_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.stamp")
	add_custom_command(
		OUTPUT  "${BGPU_TEST_SHADER_STAMP}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests/bgpu"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/tests/bgpu"
		COMMAND ${CMAKE_COMMAND} -E touch "${BGPU_TEST_SHADER_STAMP}"
		DEPENDS ${BGPU_TEST_SHADER_SOURCES}
		COMMENT "Staging bgpu's test shaders"
		VERBATIM
	)
	add_custom_target(bgpu_copy_test_shaders DEPENDS "${BGPU_TEST_SHADER_STAMP}")
	add_dependencies(bgpu_tests bgpu_copy_test_shaders)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files"
		FILES ${BGPU_TESTS_SOURCES})

	enable_strict_compiler(bgpu_tests)
	enable_coverage(bgpu_tests)
endif()
