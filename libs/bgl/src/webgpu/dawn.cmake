# Provides the imported target dawn::webgpu_dawn from Dawn's prebuilt per-platform release, which
# ships headers, a static library and a CMake package config.
#
# Dawn is not built from source: that pulls ~3 GB of dependencies into every build directory and
# costs minutes of compiling. vcpkg's `dawn` port is not an option either -- its
# 001-fix-windows-build.patch no longer applies to the source it downloads, at every version in
# the version database.

set(DAWN_RELEASE "v20260720.160313")
set(DAWN_COMMIT  "0bc38adde72b79013536f8ce354b639ae19ae195")

if(GENERATOR_IS_MULTI_CONFIG)
    message(FATAL_ERROR "The WebGPU backend needs a single-config generator: the Dawn build to "
                        "download is chosen from CMAKE_BUILD_TYPE, which a multi-config generator "
                        "does not set. Use a Ninja preset.")
endif()

# Only MSVC forces the choice: its debug and release runtime libraries cannot be mixed in one link.
# Elsewhere the release build links into a debug binary fine, and Dawn validates the WebGPU API in
# either build, so the 142 MB debug archive buys nothing over the 4 MB release one.
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_dawn_config "Debug")
else()
    set(_dawn_config "Release")
endif()

if(WIN32)
    set(_dawn_platform "windows-latest")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(_dawn_platform "macos-15-intel")
elseif(APPLE)
    set(_dawn_platform "macos-latest")
else()
    message(FATAL_ERROR "No prebuilt Dawn is wired up for this platform; add its release asset here.")
endif()

set(_dawn_hash_windows-latest_Debug
    "1ebd5f4b72669d8eff114abd9608104d69e7f53ca2b6c4833def6f49dd9842d1f17bc3a44732ca4c95f0053455c529a758f7ce5822f816e2c14d941ee2ca0ddc")
set(_dawn_hash_windows-latest_Release
    "2b0ecc6babf44e9682e7e0b90d844158738e66e66a1ff41d2d5592bc6db61062ee9c1d36f5e172fed7dcf8e321e397b8f4ad0a2819c199a9ae4929f8db332100")
set(_dawn_hash_macos-latest_Release
    "8002c80bcef83ce00a2864742d1a1626473a6041f0ca2aaef64fbf6b863319f35315548dcde05f2503329678e2ca7d7a4637ec64cfa44a30b4f018ec50237ec3")
set(_dawn_hash_macos-15-intel_Release
    "cfc505050a71e24f8cd8005cda3ab915c4eb1cd1d99a438efadd75b07c81b641a0b44f0bdd3b28202fe5a121c84fb0f771f84e0a71e2c418ea4c968b39d7df16")

set(_dawn_archive "Dawn-${DAWN_COMMIT}-${_dawn_platform}-${_dawn_config}")

include(FetchContent)

FetchContent_Declare(dawn
    URL      "https://github.com/google/dawn/releases/download/${DAWN_RELEASE}/${_dawn_archive}.tar.gz"
    URL_HASH "SHA512=${_dawn_hash_${_dawn_platform}_${_dawn_config}}"
)

# No CMakeLists.txt in the archive, so this only downloads and extracts.
FetchContent_MakeAvailable(dawn)

find_package(Threads REQUIRED)  # dawn::webgpu_dawn's interface links Threads::Threads

find_package(Dawn CONFIG REQUIRED
    PATHS "${dawn_SOURCE_DIR}/lib/cmake/Dawn"
    NO_DEFAULT_PATH
)
