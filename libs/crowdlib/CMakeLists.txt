# The crowd simulation's library: compute on a queue of its own, on the device the application's
# bgpu context owns and the renderer borrows too. It links bgpu, which carries the RHI, and never the
# renderer; the hash-fill job drives D3D12 and Metal itself from src/d3d12 and src/metal. It shares
# the context's process-wide state, so it is built as the renderer is (docs/core_process.md,
# "Linkage"). The editor does not link it. See docs/crowdlib.md.
file(GLOB_RECURSE CROWDLIB_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(FILTER CROWDLIB_SOURCES EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")

file(GLOB_RECURSE CROWDLIB_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/crowdlib/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

if(RENDERER_BACKEND STREQUAL "DX12")
	file(GLOB CROWDLIB_BACKEND_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.cpp)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	file(GLOB CROWDLIB_BACKEND_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/metal/*.cpp)
else()
	message(FATAL_ERROR "crowdlib needs a device: RENDERER_BACKEND must be DX12 or METAL")
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files"
	FILES ${CROWDLIB_SOURCES} ${CROWDLIB_HEADERS} ${CROWDLIB_BACKEND_SOURCES})

add_library(crowdlib ${BERNINI_RENDERER_LIBRARY_TYPE}
	${CROWDLIB_SOURCES}
	${CROWDLIB_HEADERS}
	${CROWDLIB_BACKEND_SOURCES}
)

if (BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "SHARED")
	target_compile_definitions(crowdlib
		PUBLIC CROWD_SHARED
		PRIVATE CROWD_EXPORTS)
endif()

target_include_directories(crowdlib
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(crowdlib PUBLIC bgpu core)

target_precompile_headers(crowdlib PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

if(RENDERER_BACKEND STREQUAL "DX12")
	target_link_libraries(crowdlib PRIVATE d3d12)
endif()

# The kernels are compiled at runtime through the context's sessions, which resolve modules from
# ./shaders/src, so they are staged there -- under crowd/, which no other copier writes. Ordered
# after bgpu's copier, whose tree they import, so the root's make_directory never races.
file(GLOB_RECURSE CROWDLIB_SHADER_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.slang
)

set(CROWDLIB_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shaders.stamp")
add_custom_command(
	OUTPUT  "${CROWDLIB_SHADER_STAMP}"
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src/crowd"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/crowd"
			"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src/crowd"
	COMMAND ${CMAKE_COMMAND} -E touch "${CROWDLIB_SHADER_STAMP}"
	DEPENDS ${CROWDLIB_SHADER_SOURCES}
	COMMENT "Staging crowdlib's Slang sources"
	VERBATIM
)
add_custom_target(crowdlib_copy_shaders ALL DEPENDS "${CROWDLIB_SHADER_STAMP}")
add_dependencies(crowdlib_copy_shaders bgpu_copy_shaders)
add_dependencies(crowdlib crowdlib_copy_shaders)

# DXIL validation at build time, as the renderer's programs have: nothing loads the output, and a
# construct Metal accepts that DXC rejects stops the Windows build here rather than at runtime.
if(RENDERER_BACKEND STREQUAL "DX12")
	set(SLANG_COMPILE_OUTS "")
	foreach(SHADER ${CROWDLIB_SHADER_SOURCES})
		file(RELATIVE_PATH SHADER_REL "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src" "${SHADER}")
		get_filename_component(SHADER_REL_DIR "${SHADER_REL}" DIRECTORY)
		compile_shader(
			FILE     "${SHADER}"
			OUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/shader_check/${SHADER_REL_DIR}"
			TARGET   "dxil"
			DXC      "$<TARGET_FILE:Microsoft::DirectXShaderCompiler>"
			INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src"
		)
	endforeach()
	add_custom_target(crowdlib_check_shaders ALL DEPENDS ${SLANG_COMPILE_OUTS})
	add_dependencies(crowdlib crowdlib_check_shaders)
endif()

enable_strict_compiler(crowdlib)
enable_coverage(crowdlib)

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE CROWDLIB_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(crowdlib_tests ${CROWDLIB_TESTS_SOURCES})

	target_include_directories(crowdlib_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
	)

	# bgl_extended for the case that runs beside a renderer on the same context; the library itself
	# never links it.
	target_link_libraries(crowdlib_tests
		PRIVATE
			crowdlib
			bgpu
			bgl_extended
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	if(RENDERER_BACKEND STREQUAL "DX12")
		target_link_libraries(crowdlib_tests PRIVATE bgl_d3d12_agility)
	endif()

	target_precompile_headers(crowdlib_tests
		PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	set_target_properties(crowdlib_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files"
		FILES ${CROWDLIB_TESTS_SOURCES})

	enable_strict_compiler(crowdlib_tests)
	enable_coverage(crowdlib_tests)
endif()
