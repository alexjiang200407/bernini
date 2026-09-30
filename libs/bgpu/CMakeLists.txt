# The GPU layer, owned by no renderer: the process's device and the compiler for it, which the
# application creates and hands to every library that runs work on the device, and the RHI each of
# them builds its own queues, resource managers and pipelines from -- bgl_extended, and a client that
# runs its own compute beside the frame. It names both backends, and it holds process-wide state, so
# it is built as the renderer is (docs/core_process.md, "Linkage").
file(GLOB_RECURSE BGPU_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(FILTER BGPU_SOURCES EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")

file(GLOB_RECURSE BGPU_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgpu/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
list(FILTER BGPU_HEADERS EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")

# The backend's half: the device creation, the RHI's implementation, and on Metal the one
# translation unit that emits metal-cpp's symbols for the whole process. Without a backend only the
# compiler and the RHI's neutral half are built -- bgl_extended_objects still compiles against them
# -- and CreateGpuContext and CreateDevice have no definition.
set(BGPU_BACKEND_DIR "")
if(RENDERER_BACKEND STREQUAL "DX12")
	set(BGPU_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12")
elseif(RENDERER_BACKEND STREQUAL "METAL")
	set(BGPU_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/metal")
endif()
if(BGPU_BACKEND_DIR)
	file(GLOB_RECURSE BGPU_BACKEND_SOURCES CONFIGURE_DEPENDS ${BGPU_BACKEND_DIR}/*.cpp)
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
	# PUBLIC: <bgpu/d3d12/native_device.h> names ID3D12Device from these headers.
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
endif()

enable_strict_compiler(bgpu)
enable_coverage(bgpu)

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
add_dependencies(bgl_copy_contract_shaders bgpu_copy_shaders)

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

# The C++ mirrors of bgpu's IDL, generated per backend into the build tree as the renderer's are
# (libs/bgl_common/idl/CMakelists.txt, which defines the generator). Only the modules with a concrete
# layout: the offset primitives are generics, and their C++ halves are hand-written in include/bgpu/idl.
set(BGPU_IDL_ROOT "${BGPU_SHADER_ROOT}/idl")
set(BGPU_IDL_OUT_ROOT "${CMAKE_BINARY_DIR}/generated/bgpu_idl")
set(BGPU_IDL_CPP_SOURCES
	DebugRecord.slang
	ErrorCode.slang
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
		COMMAND bgl_idlgen
				--src-root "${BGPU_IDL_ROOT}"
				${BGPU_IDL_LAYOUT_ARGS}
				--cpp-out-dir "${BGPU_IDL_OUT_ROOT}/bgpu/idl"
				--namespace "bgpu::idl"
				-I "${BGPU_IDL_ROOT}"
				"${BGPU_IDL_ROOT}/${module}"
		DEPENDS bgl_idlgen "${BGPU_IDL_ROOT}/${module}"
		COMMENT "idlgen bgpu ${module}"
		VERBATIM
	)
	list(APPEND BGPU_IDL_OUTPUTS "${output}")
endforeach()
add_custom_target(bgpu_idl_generate ALL DEPENDS ${BGPU_IDL_OUTPUTS})
add_dependencies(bgpu bgpu_idl_generate)
target_include_directories(bgpu PUBLIC ${BGPU_IDL_OUT_ROOT})

# What this target does NOT link is the point: every public header compiles alone -- no PCH, and
# bgpu's own closure only -- so one that reaches into the renderer (bgl, bgl_common, bgl_extended) or
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

# The suite needs a device, so there is none without a backend.
if (BUILD_TESTS AND NOT RENDERER_BACKEND STREQUAL "NONE")
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE BGPU_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(bgpu_tests ${BGPU_TESTS_SOURCES})

	# The backend's own root too: a backend-only case reaches one of its classes, which include each
	# other unprefixed.
	target_include_directories(bgpu_tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
			${BGPU_BACKEND_DIR}
	)

	if(RENDERER_BACKEND STREQUAL "DX12")
		# The Agility SDK's two exports must be the executable's, and the DLLs a device and a compile
		# load (D3D12Core, dxcompiler, dxil) are staged beside the executables by bgl_extended's build
		# -- a staging dependency, not a link: nothing here links the renderer.
		target_link_libraries(bgpu_tests PRIVATE d3d12 bgl_d3d12_agility)
		add_dependencies(bgpu_tests bgl_extended)
		# A shared bgpu exports only what BGPU_API marks, and the allocator it tests directly is not
		# part of that surface, so the case compiles its own copy.
		if(BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "SHARED")
			target_sources(bgpu_tests PRIVATE ${BGPU_BACKEND_DIR}/resource/DescriptorAllocator_d3d12.cpp)
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

	# The session cases load modules from ./shaders/src beside the suite, staged by the contract and
	# the shared tree exactly as a renderer resolves them.
	add_dependencies(bgpu_tests bgpu_copy_shaders bgl_copy_contract_shaders bgl_common_copy_shaders)

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
