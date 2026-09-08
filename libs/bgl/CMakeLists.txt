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
