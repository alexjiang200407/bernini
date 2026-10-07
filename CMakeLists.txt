cmake_minimum_required(VERSION 3.24)

# The workspace's one unpacked vcpkg tree, which its games already read, so an engine package and
# the game consuming it resolve the same ports. Only where no project() has run yet -- embedded,
# vcpkg has already settled on the consumer's -- and only for a fresh build dir, since vcpkg caches
# the choice.
if (NOT DEFINED CMAKE_PROJECT_NAME AND NOT DEFINED VCPKG_INSTALLED_DIR
    AND DEFINED ENV{WS_VCPKG_INSTALLED_DIR})
    set(VCPKG_INSTALLED_DIR "$ENV{WS_VCPKG_INSTALLED_DIR}")
    message(STATUS "Bernini: vcpkg packages shared at ${VCPKG_INSTALLED_DIR}")
endif()

project(BerniniEngine)

# This checkout, whether or not it is the top of the build. CMAKE_SOURCE_DIR is the top-level
# source directory of the *build*, so under a game's add_subdirectory it is the game's root and
# every path built from it points at a file that is not there. Nothing below may name it; the
# scripts_tests suite fails the build's own tests if one does. See docs/embedding.md.
set(BERNINI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Group targets into folders for IDE generators (Visual Studio / Xcode).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(${BERNINI_ROOT}/cmake/target_force_include.cmake)
include(${BERNINI_ROOT}/cmake/compile_shader.cmake)
include(${BERNINI_ROOT}/cmake/enable_strict_compiler.cmake)
include(${BERNINI_ROOT}/cmake/enable_coverage.cmake)
include(${BERNINI_ROOT}/cmake/enable_compiler_cache.cmake)
include(${BERNINI_ROOT}/cmake/assign_folder.cmake)
include(${BERNINI_ROOT}/cmake/strip_msvc_only_flags.cmake)
include(${BERNINI_ROOT}/cmake/deploy_qt_runtime.cmake)

# --- What a preset would otherwise have decided ------------------------------
#
# Only CMakePresets.json sets these, and a project that embeds the engine does not use it. The
# defaults are the host's, so an embedded build names only what it wants to change.
#
# Unconditional rather than nested-only: a vcpkg port configures this tree top-level, so a default
# keyed on PROJECT_IS_TOP_LEVEL would hand a port build different settings from the same engine
# added as a subdirectory.
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_bernini_platform WINDOWS)
    set(_bernini_backend  DX12)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_bernini_platform MACOS)
    set(_bernini_backend  METAL)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # core has a web platform layer; no renderer targets it.
    set(_bernini_platform WEB)
    set(_bernini_backend  NONE)
else()
    # Left empty so libs/core reports the platform it was given rather than one guessed for it.
    set(_bernini_platform "")
    set(_bernini_backend  NONE)
endif()

set(PLATFORM "${_bernini_platform}" CACHE STRING
    "Platform layer core compiles: WINDOWS, MACOS or WEB")
set_property(CACHE PLATFORM PROPERTY STRINGS WINDOWS MACOS WEB)

# NONE is a configuration, not a failure: bgl_objects compiles without a backend and
# nothing that needs CreateGraphics is declared. It is spelled rather than left unset because the
# default above means unset is no longer reachable.
#
# VULKAN is a backend bgpu creates a GPU context on and nothing draws with yet: it has no RHI and
# no renderer, so everything above bgpu builds as it does for NONE (docs/bgpu.md, "Vulkan").
set(RENDERER_BACKEND "${_bernini_backend}" CACHE STRING
    "Graphics backend: DX12, METAL, VULKAN for the GPU context alone, or NONE for bgl's objects alone")
set_property(CACHE RENDERER_BACKEND PROPERTY STRINGS DX12 METAL VULKAN NONE)
if (NOT RENDERER_BACKEND MATCHES "^(DX12|METAL|VULKAN|NONE)$")
    message(FATAL_ERROR
        "RENDERER_BACKEND must be DX12, METAL, VULKAN or NONE (got '${RENDERER_BACKEND}')")
endif()

# Whether bgl can be built: CreateGraphics is defined inside a renderer backend.
set(BERNINI_HAS_RENDERER OFF)
if (RENDERER_BACKEND MATCHES "^(DX12|METAL)$")
    set(BERNINI_HAS_RENDERER ON)
endif()

# Shader debug info and the dbg_raise() bodies, in lockstep with BERNINI_GPU_DEBUG below. A
# multi-config generator has no CMAKE_BUILD_TYPE, so there it stays a choice the preset makes.
set(_bernini_is_debug OFF)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_bernini_is_debug ON)
endif()
set(IS_DEBUG ${_bernini_is_debug} CACHE BOOL "Compile shaders with debug info and GPU asserts")

# Tracy compiles the zones in, and with them a client that opens a socket and announces itself so a
# profiler can find it -- the mechanism, not a side effect, and not something a shipping binary
# should carry. Every developer preset turns it on (CMakePresets.json); the default is off so a
# build that asked for no preset carries none of it. See docs/profiling.md.
option(BERNINI_PROFILING "Compile Tracy zones and its client into the build" OFF)

# Before any add_subdirectory: a compiler launcher is read when a target is created.
enable_compiler_cache()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMake's 23 is MSVC's /std:c++latest, which admits C++26 on VS 2026; /std:c++23preview (19.43+)
# does not.
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.43)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23preview")
endif()

add_compile_definitions($<$<CONFIG:Debug>:BERNINI_GPU_DEBUG>)

# Read by libc++ alone: MSVC's debug runtime already aborts on an empty optional dereferenced or an
# index past the end, and without this a Debug build on macOS lets both through silently.
add_compile_definitions($<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG>)


# Embedded, these are the consumer's to choose and a `set()` here would not reach its executable
# anyway -- so the engine stages its shaders and assets into whatever the consumer picked. It has
# to have picked something: an unset runtime directory stages them to a relative path, which is a
# build that succeeds and a binary that cannot find its shaders.
if (PROJECT_IS_TOP_LEVEL)
    if(CMAKE_MAKE_PROGRAM MATCHES "msbuild|MSBUILD")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()

    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
elseif (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    message(FATAL_ERROR
        "CMAKE_RUNTIME_OUTPUT_DIRECTORY is unset. Bernini stages its shaders and assets there and "
        "your executable has to land in the same directory to find them at runtime, so set it "
        "before add_subdirectory(). See docs/embedding.md.")
endif()

file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${BERNINI_ROOT}/assets/*")

set(ASSET_STAMP "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/.assets.stamp")

# Removed first because copy_directory merges: a renamed asset would otherwise linger here and a
# suite would keep passing against the stale copy.
add_custom_command(
    OUTPUT "${ASSET_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BERNINI_ROOT}/assets"
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

if (PROJECT_IS_TOP_LEVEL AND BERNINI_HAS_RENDERER)
    find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
endif()

# A top-level editor build owns one copy of every implementation a native plugin calls. Games
# embedded with add_subdirectory, renderer-free tools and builds without the editor stay static.
set(_bernini_editor_sdk_default OFF)
set(_bernini_editor_sdk_eligible OFF)
if (PROJECT_IS_TOP_LEVEL AND BERNINI_HAS_RENDERER AND Qt6_FOUND)
    set(_bernini_editor_sdk_default ON)
    set(_bernini_editor_sdk_eligible ON)
endif()
option(BERNINI_EDITOR_SDK "Build the shared libraries and build-tree package for editor plugins"
    ${_bernini_editor_sdk_default})
if (BERNINI_EDITOR_SDK AND NOT _bernini_editor_sdk_eligible)
    message(FATAL_ERROR "BERNINI_EDITOR_SDK requires a top-level editor build with Qt6")
endif()

# A game is one binary unless it asks for the renderer as a DLL. The editor SDK asks: its plugins
# are DLLs that reach the renderer through shared gamelib, and the editor links the renderer too,
# so a static one would be two. core_process follows, since it is shared exactly where something
# beside the executable needs it. See docs/core_process.md, "Linkage".
option(BERNINI_SHARED_RENDERER "Build bgl as a shared library (BERNINI_EDITOR_SDK implies it)"
    OFF)
if (BERNINI_SHARED_RENDERER AND NOT BERNINI_HAS_RENDERER)
    message(FATAL_ERROR
        "BERNINI_SHARED_RENDERER needs a renderer, and RENDERER_BACKEND is ${RENDERER_BACKEND}")
endif()
set(_bernini_renderer_library_type STATIC)
if (BERNINI_SHARED_RENDERER OR BERNINI_EDITOR_SDK)
    set(_bernini_renderer_library_type SHARED)
endif()
# Cached so a script can read what a build directory made of it (scripts/embed.py).
set(BERNINI_RENDERER_LIBRARY_TYPE ${_bernini_renderer_library_type} CACHE INTERNAL
    "STATIC or SHARED: what bgl and core_process are built as")

if (BERNINI_EDITOR_SDK)
    set(_bernini_build_id_file "${CMAKE_BINARY_DIR}/bernini-editor-build-id.txt")
    if (NOT EXISTS "${_bernini_build_id_file}")
        string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef BERNINI_EDITOR_BUILD_ID)
        file(WRITE "${_bernini_build_id_file}" "${BERNINI_EDITOR_BUILD_ID}\n")
    else()
        file(READ "${_bernini_build_id_file}" BERNINI_EDITOR_BUILD_ID)
        string(STRIP "${BERNINI_EDITOR_BUILD_ID}" BERNINI_EDITOR_BUILD_ID)
    endif()
endif()

find_package(ktx CONFIG REQUIRED)
set_target_properties(KTX::ktx PROPERTIES
    MAP_IMPORTED_CONFIG_DEBUG          Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release)

# Found once, here, for the reason above: its config script declares an ALIAS for the Lua plugin,
# and a second find_package in another directory fails outright trying to declare it twice. Both
# gamelib (the runtime) and examples/util (the SDL translation) name the targets.
find_package(RmlUi CONFIG REQUIRED)

# None of these three is namespaced, and BUILD_TESTS and BUILD_COVERAGE are names a consuming
# project is likely to have taken for its own suites and its own coverage -- the second would
# otherwise fail the engine's configure outright under any compiler but clang. Scoped to this
# directory and everything under it, so whatever the consumer set still reaches the consumer's
# targets and never ours.
if (NOT PROJECT_IS_TOP_LEVEL)
    set(BUILD_TESTS OFF)
    set(BERNINI_BUILD_EXAMPLES OFF)
    set(BUILD_COVERAGE OFF)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/assetlib_structs)
# The terrain generator: a heightfield from a seed, linking the container and core alone.
add_subdirectory(libs/terrainlib)
# Between the contract and the renderer: what every renderer needs and no renderer owns.
add_subdirectory(libs/bgpu)
set(FG_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/bgl)
add_subdirectory(libs/assetlib)
# gamelib is the seam: the only library that links both the renderer and the asset layer.
if (TARGET bgl)
    add_subdirectory(libs/gamelib)
    # Compute beside the renderer on the device they share; it links bgpu, never bgl.
    add_subdirectory(libs/crowdlib)
    # The crowd drawn GPU to GPU: the one library that links both the renderer and the crowd.
    add_subdirectory(libs/crowd_render)
endif()

# Qt is not a vcpkg dependency, so an embedded build would look for one the consumer never asked
# for and occasionally find it.
if (PROJECT_IS_TOP_LEVEL)
    if (Qt6_FOUND AND TARGET bgl)
        add_subdirectory(apps/editor)
    elseif (NOT Qt6_FOUND)
        message(STATUS "Qt6 not found - skipping editor (set CMAKE_PREFIX_PATH to your Qt install to enable it)")
    endif()
endif()

# Outside the examples, which the release presets leave off: release is the build its benchmarks
# are taken on.
if (PROJECT_IS_TOP_LEVEL AND TARGET bgl)
    add_subdirectory(apps/headless)
    add_subdirectory(apps/ai_viewer)
endif()

if (BERNINI_BUILD_EXAMPLES AND TARGET bgl)
    add_subdirectory(examples)
endif()

assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/core"             "misc")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/bgpu"   "gfx")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib"         "editor")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib_structs" "editor")

if (TARGET gamelib)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/gamelib" "misc")
endif()
if (TARGET crowdlib)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/crowdlib" "gfx")
endif()
if (BERNINI_BUILD_EXAMPLES AND TARGET bgl)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/examples" "examples")
endif()
if (TARGET editor)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/apps/editor" "editor")
endif()
if (TARGET bgl_ai_viewer)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/apps/headless"  "apps")
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/apps/ai_viewer" "apps")
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

# A game built against this build rather than compiled into its own: every build directory carries
# the package for exactly what it built, so a consumer's preset names the engine build it matches.
# Top-level only -- embedded, the consumer already has the targets. See docs/embedding.md.
if (PROJECT_IS_TOP_LEVEL AND TARGET gamelib)
    set(BERNINI_PACKAGE_DIR "${CMAKE_BINARY_DIR}/bernini_sdk")
    set(BERNINI_PACKAGE_TARGETS
        core_process core assetlib_structs bgpu bgl_headers bgl_idl bgl_objects
        bgl assetlib gamelib crowdlib_objects crowdlib)
    # Exports the Agility SDK's version from the executable; it must be linked into the consumer's.
    if (RENDERER_BACKEND STREQUAL "DX12")
        list(APPEND BERNINI_PACKAGE_TARGETS bgpu_d3d12_agility)
        # A static renderer's PRIVATE link of its backend is part of what it exports: the consumer
        # links the backend's system libraries itself.
        if (BERNINI_RENDERER_LIBRARY_TYPE STREQUAL "STATIC")
            list(APPEND BERNINI_PACKAGE_TARGETS bgl_d3d12)
        endif()
    endif()

    file(MAKE_DIRECTORY "${BERNINI_PACKAGE_DIR}")
    export(TARGETS ${BERNINI_PACKAGE_TARGETS}
        NAMESPACE Bernini::
        FILE "${BERNINI_PACKAGE_DIR}/BerniniTargets.cmake")
    add_custom_target(bernini_package)
    add_dependencies(bernini_package ${BERNINI_PACKAGE_TARGETS})
    configure_file(
        "${BERNINI_ROOT}/cmake/BerniniConfig.cmake.in"
        "${BERNINI_PACKAGE_DIR}/BerniniConfig.cmake"
        @ONLY)
endif()
