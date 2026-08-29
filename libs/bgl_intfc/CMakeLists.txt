# bgl's public surface: the contract a renderer implements, owned by no renderer.
#
# The headers live here rather than under libs/bgl so that `bgl` names one implementation of this
# interface instead of naming both the interface and an implementation of it. The <bgl/...> include
# spelling and the bgl:: namespace are unchanged -- the include/bgl/ suffix moved with them.

file(GLOB_RECURSE BGL_INTFC_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.hpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/bgl" PREFIX "Header Files"
	FILES ${BGL_INTFC_HEADERS})

add_library(bgl_intfc INTERFACE)

target_include_directories(bgl_intfc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The entire closure: core carries Ref/SharedRef/slot_handle, assetlib_structs the decoded PODs. glm
# is reached through <core/glm.h>, and core links glm::glm PUBLIC, so it needs no entry of its own.
target_link_libraries(bgl_intfc INTERFACE core assetlib_structs)

# An INTERFACE target compiles nothing, so on its own it enforces nothing. This one TU is the
# enforcement -- see src/selfcheck.cpp for why it is a library and not an executable.
add_library(bgl_intfc_selfcheck STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp
	${BGL_INTFC_HEADERS}
)

target_precompile_headers(bgl_intfc_selfcheck PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h)

# PRIVATE, and bgl_intfc only: linking anything else would defeat the check.
target_link_libraries(bgl_intfc_selfcheck PRIVATE bgl_intfc)

enable_strict_compiler(bgl_intfc_selfcheck)
