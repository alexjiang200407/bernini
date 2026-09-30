find_package(slang CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

# The generator every IDL tree runs: bgpu's own below, and the renderer's
# (libs/bgl/idl/CMakeLists.txt). It lives with the lowest of them.
add_executable(bgpu_idlgen
    ${CMAKE_CURRENT_SOURCE_DIR}/idlgen.cpp
)

target_precompile_headers(bgpu_idlgen
	PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
)

target_link_libraries(bgpu_idlgen PUBLIC slang::slang CLI11::CLI11 core)

# No enable_strict_compiler here. This switches over slang's reflection enums and handles the subset
# it generates for, throwing on the rest -- so /Wall's C4061 fires once per unhandled enumerator, and
# naming them all would break the build on every slang bump to add cases that reach the same throw.
# The warning earns its keep on our own enums, where a new one falling into `default:` is a bug.
