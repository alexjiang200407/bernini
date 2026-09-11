# The containers every subsystem speaks, and nothing else.
#
# Data by rule: structs, the constants that address them and the static_asserts that pin their
# layout. No function lives here, so there is nothing to compile and this is an INTERFACE target --
# which is what it always was in effect, since its one translation unit was an empty placeholder.
#
# The rule is about who can link what. bgl and bgl_extended link this and never assetlib, so a
# function placed here is one the renderer may call; keeping the surface to data is what makes
# "the renderer cannot ask a container anything" true rather than merely observed. A question about
# a container is answered in assetlib -- see the root CLAUDE.md.

file(GLOB_RECURSE ASSETLIB_STRUCTS_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)

add_library(assetlib_structs INTERFACE)

target_include_directories(assetlib_structs
	INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetlib_structs INTERFACE core)

# An INTERFACE target compiles nothing, so on its own it enforces nothing. This one TU is the
# enforcement -- see src/selfcheck.cpp. A library and not an executable: there is nothing to link,
# because a header here declares nothing anyone has to define.
add_library(assetlib_structs_selfcheck STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/selfcheck.cpp
	${ASSETLIB_STRUCTS_HEADERS}
)

# PRIVATE, and assetlib_structs only: linking assetlib would defeat the check.
target_link_libraries(assetlib_structs_selfcheck PRIVATE assetlib_structs)

enable_strict_compiler(assetlib_structs_selfcheck)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/assetlib_structs"
	PREFIX "Header Files" FILES ${ASSETLIB_STRUCTS_HEADERS})
