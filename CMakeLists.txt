# CMakeList.txt : CMake project for Bernini Game Engine, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

macro(set_from_environment_ VARIABLE ENV_VAR)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${ENV_VAR}})
		set(${VARIABLE} $ENV{${ENV_VAR}})
	endif ()
endmacro()

macro(set_from_environment VARIABLE)
	set_from_environment_(${VARIABLE} ${VARIABLE})
endmacro()

set_from_environment(VCPKG_ROOT)

if (DEFINED VCPKG_ROOT)
	set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
	set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
else ()
	message(
		WARNING
		"Variable VCPKG_ROOT is not set. Continuing without vcpkg."
	)
endif ()


project ("Bernini" LANGUAGES CXX)

include(cmake/sourcelist.cmake)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

# Add source to this project's executable.

add_executable (Bernini WIN32 ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE d3d11.lib)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${OPENGL_INCLUDE_DIR}
)

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		include/PCH.h
)


if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Bernini PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
