cmake_minimum_required(VERSION 3.24)
project(BerniniEngine)

# Group targets into folders for IDE generators (Visual Studio / Xcode).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(${CMAKE_SOURCE_DIR}/cmake/target_force_include.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/copy_to_target.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compile_shader.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/enable_strict_compiler.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/enable_coverage.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/enable_compiler_cache.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/assign_folder.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/strip_msvc_only_flags.cmake)

# Tracy compiles the zones in, and with them a client that opens a socket and announces itself so a
# profiler can find it -- the mechanism, not a side effect, and not something a shipping binary
# should carry. Every developer preset turns it on (CMakePresets.json); the default is off so a
# build that asked for no preset carries none of it. See docs/profiling.md.
option(BERNINI_PROFILING "Compile Tracy zones and its client into the build" OFF)

# Before any add_subdirectory: a compiler launcher is read when a target is created.
enable_compiler_cache()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions($<$<CONFIG:Debug>:BERNINI_GPU_DEBUG>)


if(CMAKE_MAKE_PROGRAM MATCHES "msbuild|MSBUILD")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*")

set(ASSET_STAMP "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/.assets.stamp")

# Removed first because copy_directory merges: a renamed asset would otherwise linger here and a
# suite would keep passing against the stale copy.
add_custom_command(
    OUTPUT "${ASSET_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${ASSET_STAMP}"
    DEPENDS ${ASSET_FILES}
    COMMENT "Copying assets to the runtime output directory"
    VERBATIM
)

add_custom_target(copy_assets DEPENDS "${ASSET_STAMP}")

# --- Force the optimized (Release) libktx, even in Debug builds ---
# Before any subdirectory, and GLOBAL: an imported target is directory-scoped by default, so
# libs/assetlib's own find_package(ktx) would otherwise get a second KTX::ktx that this mapping
# never reaches -- and link the debug encoder. Where ktx is a shared library the Release copy
# deployed next to the executable hides that at runtime; where it is static (macOS) the debug
# code is linked in, and a texture bake runs an order of magnitude slower.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
find_package(ktx CONFIG REQUIRED)
set_target_properties(KTX::ktx PROPERTIES
    MAP_IMPORTED_CONFIG_DEBUG          Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release)

# Found once, here, for the reason above: its config script declares an ALIAS for the Lua plugin,
# and a second find_package in another directory fails outright trying to declare it twice. Both
# gamelib (the runtime) and examples/util (the SDL translation) name the targets.
find_package(RmlUi CONFIG REQUIRED)

add_subdirectory(libs/core)
add_subdirectory(libs/assetlib_structs)
add_subdirectory(libs/bgl)
# Between the contract and the renderer: what every renderer needs and no renderer owns.
add_subdirectory(libs/bgl_common)
set(FG_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/bgl_extended)
add_subdirectory(libs/assetlib)
# gamelib is the seam: the only library that links both the renderer and the asset layer.
if (TARGET bgl_extended)
    add_subdirectory(libs/gamelib)
endif()

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
if (Qt6_FOUND AND TARGET bgl_extended)
    add_subdirectory(apps/editor)
elseif (NOT Qt6_FOUND)
    message(STATUS "Qt6 not found - skipping editor (set CMAKE_PREFIX_PATH to your Qt install to enable it)")
endif()

if (BERNINI_BUILD_EXAMPLES AND TARGET bgl_extended)
    add_subdirectory(examples)
endif()

assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/core"             "misc")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/bgl_common"       "gfx")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/bgl_extended"           "gfx")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib"         "editor")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib_structs" "editor")

if (TARGET gamelib)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/gamelib" "misc")
endif()
if (BERNINI_BUILD_EXAMPLES AND TARGET bgl_extended)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/examples" "examples")
endif()
if (TARGET editor)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/apps/editor" "editor")
endif()


# Every executable in the tree, however deep, and whatever it links.
#
# vcpkg's applocal step runs as each executable links and *reads* the runtime output directory to
# work out what to deploy beside it. The copy below writes into that same directory, so the two
# must be ordered -- on Windows an overlap fails the link outright with
# `open_for_read(...\ktx.dll): permission denied`, from whichever target happened to be linking.
function(bernini_collect_executables dir out_var)
    set(_found "")

    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if (_type STREQUAL "EXECUTABLE")
            list(APPEND _found ${_target})
        endif()
    endforeach()

    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        # FetchContent adds its dependencies' source trees as subdirectories, and what those ship is
        # not ours to order or to build. Collecting one would put it in ALL by the dependency below.
        cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${_subdir}" NORMALIZE _vendored)
        if (_vendored)
            continue()
        endif()

        bernini_collect_executables("${_subdir}" _from_subdir)
        list(APPEND _found ${_from_subdir})
    endforeach()

    set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

bernini_collect_executables("${CMAKE_CURRENT_SOURCE_DIR}" BERNINI_EXECUTABLES)

if (BERNINI_EXECUTABLES)
    add_custom_target(deploy_release_ktx ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:KTX::ktx>" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMENT "Deploying optimized Release libktx to the runtime output directory"
        VERBATIM)
    add_dependencies(deploy_release_ktx ${BERNINI_EXECUTABLES})
endif()

# applocal deploys what it finds in the installed tree's `bin`, and the Tracy port puts its debug
# DLL in `debug/bin/Debug` -- one directory below where that step looks. Nothing else in the tree
# installs that way, so nothing copies it, and every executable linking core dies at load with
# STATUS_DLL_NOT_FOUND before `main`. The imported target carries the real path.
if (BERNINI_PROFILING AND BERNINI_EXECUTABLES AND TARGET Tracy::TracyClient)
    get_target_property(TRACY_CLIENT_TYPE Tracy::TracyClient TYPE)

    if (TRACY_CLIENT_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_target(deploy_tracy_client ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Tracy::TracyClient>" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMENT "Deploying the Tracy client to the runtime output directory"
            VERBATIM)
        add_dependencies(deploy_tracy_client ${BERNINI_EXECUTABLES})
    endif()
endif()
