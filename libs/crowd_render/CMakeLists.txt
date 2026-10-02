# A crowd drawn GPU to GPU: the glue that links the renderer and the crowd, which never link each
# other (docs/crowdlib.md). It is not gamelib, which the editor links and which may therefore never
# link crowdlib. See docs/crowd_render.md.
file(GLOB_RECURSE CROWD_RENDER_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE CROWD_RENDER_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/crowd_render/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files"
	FILES ${CROWD_RENDER_SOURCES} ${CROWD_RENDER_HEADERS})

# Static: it holds no process-wide state, so a copy per binary costs nothing.
add_library(crowd_render STATIC
	${CROWD_RENDER_SOURCES}
	${CROWD_RENDER_HEADERS}
)

target_include_directories(crowd_render
	PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# The writer's IDL, generated per build as crowdlib's private modules are: "idl/<Name>.h", namespace
# crowd_render::idl.
set(CROWD_RENDER_IDL_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/crowd_render/idl")
set(CROWD_RENDER_IDL_OUT_ROOT "${CMAKE_BINARY_DIR}/generated/crowd_render")

if(RENDERER_BACKEND STREQUAL "METAL")
	set(CROWD_RENDER_IDL_LAYOUT_ARGS --metal-layout)
else()
	set(CROWD_RENDER_IDL_LAYOUT_ARGS "")
endif()

file(GLOB CROWD_RENDER_IDL_SOURCES CONFIGURE_DEPENDS "${CROWD_RENDER_IDL_SRC_ROOT}/*.slang")
set(CROWD_RENDER_IDL_OUTPUTS "")
foreach(src ${CROWD_RENDER_IDL_SOURCES})
	get_filename_component(name "${src}" NAME_WE)
	set(output "${CROWD_RENDER_IDL_OUT_ROOT}/idl/${name}.h")
	add_custom_command(
		OUTPUT  "${output}"
		COMMAND bgpu_idlgen
				--src-root "${CROWD_RENDER_IDL_SRC_ROOT}"
				${CROWD_RENDER_IDL_LAYOUT_ARGS}
				--cpp-out-dir "${CROWD_RENDER_IDL_OUT_ROOT}/idl"
				--namespace "crowd_render::idl"
				-I "${CROWD_RENDER_IDL_SRC_ROOT}"
				"${src}"
		DEPENDS bgpu_idlgen "${src}"
		COMMENT "idlgen crowd_render/idl/${name}.slang"
		VERBATIM
	)
	list(APPEND CROWD_RENDER_IDL_OUTPUTS "${output}")
endforeach()

add_custom_target(crowd_render_idl_generate ALL DEPENDS ${CROWD_RENDER_IDL_OUTPUTS})
add_dependencies(crowd_render crowd_render_idl_generate)
target_include_directories(crowd_render PRIVATE ${CROWD_RENDER_IDL_OUT_ROOT})

target_link_libraries(crowd_render PUBLIC bgl crowdlib core)

target_precompile_headers(crowd_render PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

# The writer is compiled at runtime by the renderer, which resolves it from ./shaders/src. It
# imports both the renderer's contract and the crowd's records, so it is staged after both.
file(GLOB_RECURSE CROWD_RENDER_SHADER_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.slang
)

set(CROWD_RENDER_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shaders.stamp")
add_custom_command(
	OUTPUT  "${CROWD_RENDER_SHADER_STAMP}"
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src/crowd_render"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/crowd_render"
			"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src/crowd_render"
	COMMAND ${CMAKE_COMMAND} -E touch "${CROWD_RENDER_SHADER_STAMP}"
	DEPENDS ${CROWD_RENDER_SHADER_SOURCES}
	COMMENT "Staging crowd_render's Slang sources"
	VERBATIM
)
add_custom_target(crowd_render_copy_shaders ALL DEPENDS "${CROWD_RENDER_SHADER_STAMP}")
add_dependencies(crowd_render_copy_shaders crowdlib_copy_shaders bgl_copy_contract_shaders)
add_dependencies(crowd_render crowd_render_copy_shaders)

enable_strict_compiler(crowd_render)
enable_coverage(crowd_render)

if (BUILD_TESTS)
	find_package(Catch2 CONFIG REQUIRED)

	file(GLOB_RECURSE CROWD_RENDER_TESTS_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/src/*.cpp
	)

	add_executable(crowd_render_tests ${CROWD_RENDER_TESTS_SOURCES})

	# src/ and the generated IDL too: the tests run the writer against the frame the library plans.
	target_include_directories(crowd_render_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src ${CMAKE_CURRENT_SOURCE_DIR}/src
				${CROWD_RENDER_IDL_OUT_ROOT}
	)

	target_link_libraries(crowd_render_tests
		PRIVATE
			crowd_render
			bgl
			crowdlib
			bgpu
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
	)

	if(RENDERER_BACKEND STREQUAL "DX12")
		target_link_libraries(crowd_render_tests PRIVATE bgpu_d3d12_agility)
	endif()

	# The test kernels, staged beside the library's tree rather than in ./shaders/tests, which the
	# renderer's suite stages with a writer of its own.
	file(GLOB_RECURSE CROWD_RENDER_TEST_SHADER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/*.slang
	)
	set(CROWD_RENDER_TEST_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.stamp")
	add_custom_command(
		OUTPUT  "${CROWD_RENDER_TEST_SHADER_STAMP}"
		COMMAND ${CMAKE_COMMAND} -E rm -rf
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src/crowd_render_tests"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src/crowd_render_tests"
		COMMAND ${CMAKE_COMMAND} -E touch "${CROWD_RENDER_TEST_SHADER_STAMP}"
		DEPENDS ${CROWD_RENDER_TEST_SHADER_SOURCES}
		COMMENT "Staging crowd_render's test kernels"
		VERBATIM
	)
	add_custom_target(crowd_render_copy_test_shaders DEPENDS "${CROWD_RENDER_TEST_SHADER_STAMP}")
	add_dependencies(crowd_render_copy_test_shaders crowd_render_copy_shaders)
	add_dependencies(crowd_render_tests crowd_render_copy_test_shaders crowd_render_idl_generate)

	target_precompile_headers(crowd_render_tests PRIVATE ${BERNINI_ROOT}/PCH/pch.h)

	set_target_properties(crowd_render_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files"
		FILES ${CROWD_RENDER_TESTS_SOURCES})

	enable_strict_compiler(crowd_render_tests)
	enable_coverage(crowd_render_tests)
endif()
