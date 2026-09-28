# The process's GPU device and the compiler for it, owned by no renderer: the application creates
# one and hands it to every library that runs work on the device -- bgl_extended, and a client that
# runs its own compute beside the frame. It names both backends, which bgl_common may not, and it
# holds process-wide state, so it is built as the renderer is (docs/core_process.md, "Linkage").
file(GLOB_RECURSE DEVICE_CONTEXT_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(FILTER DEVICE_CONTEXT_SOURCES EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")

file(GLOB_RECURSE DEVICE_CONTEXT_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/device_context/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
list(FILTER DEVICE_CONTEXT_HEADERS EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")

# The backend's half: the device creation, and on Metal the one translation unit that emits
# metal-cpp's symbols for the whole process. Without a backend only the compiler is built --
# bgl_extended_objects still compiles against it -- and CreateDeviceContext has no definition.
if(RENDERER_BACKEND STREQUAL "DX12")
	file(GLOB DEVICE_CONTEXT_BACKEND_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.cpp)
	file(GLOB DEVICE_CONTEXT_BACKEND_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.h)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	file(GLOB DEVICE_CONTEXT_BACKEND_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/metal/*.cpp)
	file(GLOB DEVICE_CONTEXT_BACKEND_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/metal/*.h)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files"
	FILES ${DEVICE_CONTEXT_SOURCES} ${DEVICE_CONTEXT_HEADERS}
	      ${DEVICE_CONTEXT_BACKEND_SOURCES} ${DEVICE_CONTEXT_BACKEND_HEADERS})

find_package(slang CONFIG REQUIRED)

add_library(device_context ${BERNINI_RENDERER_LIBRARY_TYPE}
	${DEVICE_CONTEXT_SOURCES}
	${DEVICE_CONTEXT_HEADERS}
	${DEVICE_CONTEXT_BACKEND_SOURCES}
	${DEVICE_CONTEXT_BACKEND_HEADERS}
)

if (BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "SHARED")
	target_compile_definitions(device_context
		PUBLIC DEVICE_CONTEXT_SHARED
		PRIVATE DEVICE_CONTEXT_EXPORTS)
endif()

target_include_directories(device_context
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(device_context PUBLIC core bgl_common slang::slang)

target_precompile_headers(device_context
	PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

if(RENDERER_BACKEND STREQUAL "DX12")
	find_package(directx-headers CONFIG REQUIRED)
	target_link_libraries(device_context PRIVATE d3d12 dxgi dxguid Microsoft::DirectX-Headers)
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

	target_include_directories(device_context SYSTEM PUBLIC ${metalcpp_SOURCE_DIR})
	target_link_libraries(device_context PUBLIC "-framework Metal" "-framework Foundation" "-framework QuartzCore")

	# metal-cpp emits its selector and class symbols in whichever TU defines the
	# *_PRIVATE_IMPLEMENTATION macros before including the headers. A PCH would include them first
	# and the definitions would be skipped, so this one file compiles without it.
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/metal/MetalImpl.cpp
		PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

enable_strict_compiler(device_context)
enable_coverage(device_context)

# The suite needs a device, so there is none without a backend.
if (BUILD_TESTS AND NOT RENDERER_BACKEND STREQUAL "NONE")
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE DEVICE_CONTEXT_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(device_context_tests ${DEVICE_CONTEXT_TESTS_SOURCES})

	target_include_directories(device_context_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
	)

	target_link_libraries(device_context_tests
		PRIVATE
			device_context
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	target_precompile_headers(device_context_tests
		PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(device_context_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	# The session cases load modules from ./shaders/src beside the suite, staged by the contract and
	# the shared tree exactly as a renderer resolves them.
	add_dependencies(device_context_tests bgl_copy_contract_shaders bgl_common_copy_shaders)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files"
		FILES ${DEVICE_CONTEXT_TESTS_SOURCES})

	enable_strict_compiler(device_context_tests)
	enable_coverage(device_context_tests)
endif()
