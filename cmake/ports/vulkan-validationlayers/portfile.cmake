# Overlay of vcpkg's vulkan-validationlayers port, forked ONLY to build the layer without mimalloc.
#
# Upstream links mimalloc statically, as the layer's own operator new and nothing else's. vcpkg's
# mimalloc is a DLL on a dynamic triplet, and that DLL imports mimalloc-redirect.dll, which patches
# the C runtime's malloc for the whole process the moment it loads -- so enabling the Vulkan debug
# layer would swap the allocator under the engine, mid-run, in whatever binary asked for validation.
# The layer would also stop being one file: bgpu stages it beside the executables
# (libs/bgpu/CMakeLists.txt), and the loader reports a layer whose imports are missing as absent.
#
# So USE_MIMALLOC is off and mimalloc is not a dependency. The cost is a slower layer, since
# validation allocates heavily. Everything else is vcpkg's port verbatim, at the version its baseline
# pins: a baseline that moves vulkan-headers moves this with it, by copying the new port over this
# directory and reapplying the two lines.

set(VCPKG_LIBRARY_LINKAGE dynamic)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KhronosGroup/Vulkan-ValidationLayers
    REF "vulkan-sdk-${VERSION}"
    SHA512 621ed3bc35d97bbac6a2343818530ac5b36c10868fb0fa8de7a3dbb9ae67f3676fd66656a36c4f19ec875b247b0c29b478d735882a429e850c1754203d4f9845
    HEAD_REF main
    PATCHES
        disable_vendored_phmap.diff
)

file(REMOVE_RECURSE "${SOURCE_PATH}/layers/external/parallel_hashmap") # ensure that we use vcpkg's parallel-hashmap instead of upstream's vendored copy

vcpkg_find_acquire_program(PYTHON3)
get_filename_component(PYTHON3_DIR "${PYTHON3}" DIRECTORY)
vcpkg_add_to_path("${PYTHON3_DIR}")

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DBUILD_TESTS:BOOL=OFF
    -DUSE_MIMALLOC:BOOL=OFF
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")

set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

set(layer_path "<vcpkg_installed>/bin")
if(NOT VCPKG_TARGET_IS_WINDOWS)
 set(layer_path "<vcpkg_installed>/share/vulkan/explicit_layer.d")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" @ONLY)
