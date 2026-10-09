# NvAPI, which vcpkg has no port for: NVIDIA publishes the SDK on GitHub as one commit per driver
# branch, with no tags or releases, so REF pins the commit. At this one License.txt puts the
# libraries under MIT and every header carries SPDX-License-Identifier: MIT (THIRD_PARTY_NOTICES.md).
#
# Only the x64 library is installed: nvapi64.lib is a static stub that finds nvapi64.dll, which the
# driver installs, at NvAPI_Initialize -- a machine with no NVIDIA driver links and runs.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO NVIDIA/nvapi
    REF 70d337db9186e968eab622f7e786de7e437faf3d
    SHA512 20c5e46510f9b5a8ca387d97af9f78424e4a549e78632e356808f3c203b1ac1ecd3ce7f113abf7aa2877284e7c678be739ac56be246158e22519296eeda1b655
    HEAD_REF main
)

file(GLOB NVAPI_HEADERS "${SOURCE_PATH}/nvapi*.h")
file(INSTALL ${NVAPI_HEADERS} DESTINATION "${CURRENT_PACKAGES_DIR}/include/nvapi")
file(INSTALL "${SOURCE_PATH}/amd64/nvapi64.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/amd64/nvapi64.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/unofficial-nvapi-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/unofficial-nvapi")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/License.txt")
