cmake_minimum_required(VERSION 3.22)
project(BerniniEngine)

# Group targets into folders for IDE generators (Visual Studio / Xcode).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(${CMAKE_SOURCE_DIR}/cmake/target_force_include.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/copy_to_target.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compile_shader.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/enable_strict_compiler.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/assign_folder.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/strip_msvc_only_flags.cmake)

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

add_custom_command(
    OUTPUT "${ASSET_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${ASSET_STAMP}"
    DEPENDS ${ASSET_FILES}
    COMMENT "Copying assets to the runtime output directory"
    VERBATIM
)

add_custom_target(copy_assets DEPENDS "${ASSET_STAMP}")

add_subdirectory(libs/core)
add_subdirectory(libs/assetlib_structs)
set(FG_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/bgl)
add_subdirectory(libs/assetlib)
# gamelib is the seam: the only library that links both the renderer and the asset layer.
if (TARGET bgl)
    add_subdirectory(libs/gamelib)
endif()

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
if (Qt6_FOUND AND TARGET bgl)
    add_subdirectory(apps/editor)
elseif (NOT Qt6_FOUND)
    message(STATUS "Qt6 not found - skipping editor (set CMAKE_PREFIX_PATH to your Qt install to enable it)")
endif()

if (BERNINI_BUILD_EXAMPLES AND TARGET bgl)
    add_subdirectory(examples)
endif()

get_property(EXAMPLE_TARGETS GLOBAL PROPERTY GLOBAL_EXAMPLE_TARGETS)

assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/core"             "misc")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/bgl"              "gfx")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib"         "editor")
assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/assetlib_structs" "editor")

if (TARGET gamelib)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/libs/gamelib" "misc")
endif()
if (BERNINI_BUILD_EXAMPLES AND TARGET bgl)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/examples" "examples")
endif()
if (TARGET editor)
    assign_folder("${CMAKE_CURRENT_SOURCE_DIR}/apps/editor" "editor")
endif()

# --- Force the optimized (Release) libktx at runtime, even in Debug builds ---
find_package(ktx CONFIG REQUIRED)
set_target_properties(KTX::ktx PROPERTIES
    MAP_IMPORTED_CONFIG_DEBUG          Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release)

set(KTX_CONSUMER_TARGETS "")
foreach(_ktx_consumer assetlib_cli assetlib_tests bgl_tests editor ${EXAMPLE_TARGETS})
    if (TARGET ${_ktx_consumer})
        list(APPEND KTX_CONSUMER_TARGETS ${_ktx_consumer})
    endif()
endforeach()

if (KTX_CONSUMER_TARGETS)
    add_custom_target(deploy_release_ktx ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:KTX::ktx>" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMENT "Deploying optimized Release libktx to the runtime output directory"
        VERBATIM)
    add_dependencies(deploy_release_ktx ${KTX_CONSUMER_TARGETS})
endif()
