set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

find_program(SLANG_EXECUTABLE
    NAMES slang slangc slangc.exe
    DOC "Slang shader compiler (slang/slangc)"
)

if(NOT SLANG_EXECUTABLE)
    message(FATAL_ERROR "Slang compiler not found. Please install Slang and put 'slang' or 'slangc' on PATH.")
endif()

set(SLANG_COMPILE_OUTS "")
set(SLANG_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
# The shared tree: idl.* resolves from here, and nothing under it is this renderer's to compile.
set(SLANG_COMMON_ROOT "${CMAKE_SOURCE_DIR}/libs/bgl_common/shaders/src")


# The list is the tree: every program under programs/ is compiled to DXIL here, at every entry
# point its own source declares. A shader that exists is a shader that is validated -- a list kept
# by hand is one a shader can be left out of, and DXIL validation is the only thing standing between
# a construct Metal accepts and a Windows runtime (see docs/slang_shaders.md).
file(GLOB_RECURSE SLANG_PROGRAM_SOURCES CONFIGURE_DEPENDS
    "${SLANG_SOURCE_ROOT}/programs/*.slang"
)

foreach(SHADER ${SLANG_PROGRAM_SOURCES})
    file(RELATIVE_PATH SHADER_REL "${SLANG_SOURCE_ROOT}" "${SHADER}")
    get_filename_component(SHADER_REL_DIR "${SHADER_REL}" DIRECTORY)

    slang_entry_points("${SHADER}" SHADER_ENTRIES)

    if(NOT SHADER_ENTRIES)
        message(FATAL_ERROR
            "${SHADER_REL} declares no [shader(...)] entry point. A module that is only imported "
            "belongs under lib/, not programs/.")
    endif()

    foreach(ENTRY IN LISTS SHADER_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY}")
        list(GET ENTRY_PARTS 0 ENTRY_STAGE)
        list(GET ENTRY_PARTS 1 ENTRY_NAME)
        slang_stage_profile("${ENTRY_STAGE}" ENTRY_PROFILE)

        # The DXIL mirrors the source tree so two programs sharing a leaf name cannot compile onto
        # each other's output. Nothing loads it; this is validation output.
        compile_shader(
            FILE         "${SHADER}"
            OUT_DIR      "${SHADER_OUT_DIR}/${SHADER_REL_DIR}"
            TARGET       "dxil"
            STAGE        "${ENTRY_PROFILE}"
            INCLUDES     ${SLANG_SOURCE_ROOT} ${SLANG_COMMON_ROOT}
            ENTRY_POINTS "${ENTRY_NAME}"
        )
    endforeach()
endforeach()

message(STATUS "Discovered Compiled Shaders: ${SLANG_COMPILE_OUTS}")


if (BUILD_TESTS)
    file(GLOB SLANG_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.slang"
    )

    foreach(SHADER ${SLANG_TEST_SOURCES})
        get_filename_component(SHADER_BARE_NAME "${SHADER}" NAME_WE)
        get_filename_component(SHADER_FILENAME "${SHADER}" NAME)

        # This build-time validation infers the stage from a VS/PS/GS/MS/AS/CS filename prefix and
        # compiles the `main` entry. A multi-stage test shader (mesh + pixel in one file, with named
        # entry points) fits neither, so skip it here -- it is still staged below and validated when
        # the test compiles it at runtime.
        if(NOT SHADER_FILENAME MATCHES "^(VS|PS|GS|MS|AS|CS)")
            continue()
        endif()

        compile_shader(
            FILE     "${SHADER}"
            OUT_DIR  "${SHADER_OUT_DIR}"
            TARGET   "dxil"
            INCLUDES ${SLANG_SOURCE_ROOT} ${SLANG_COMMON_ROOT}
        )

        list(APPEND SLANG_COMPILE_OUTS "${${SHADER_BARE_NAME}_OUTPUT}")
    endforeach()

    add_custom_target(bgl_copy_shader_tests ALL
        DEPENDS bgl_compile_shaders
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/tests"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/tests"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/tests"
        COMMENT "Copying raw Slang test shader sources to runtime output layout"
        VERBATIM
    )
endif()

add_custom_target(bgl_compile_shaders ALL
    DEPENDS ${SLANG_COMPILE_OUTS}
)

# On a multi-config generator the DXIL is compiled into a config-agnostic dir and has to be copied
# into each per-config runtime dir (bin/<Config>/shaders). On a single-config generator SHADER_OUT_DIR
# already *is* the runtime shaders dir, so the copy is a directory onto itself -- a no-op that also
# fails outright ("No such file or directory") if the dir is momentarily absent during a reconfigure.
# Skip it when the paths coincide; keep the target so its dependents still resolve.
if(NOT SHADER_OUT_DIR STREQUAL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    add_custom_target(bgl_copy_shaders ALL
        DEPENDS bgl_compile_shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SHADER_OUT_DIR}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
        COMMENT "Copying compiled DXIL binaries to runtime output layout"
        VERBATIM
    )
else()
    add_custom_target(bgl_copy_shaders ALL
        DEPENDS bgl_compile_shaders
        COMMENT "Compiled DXIL already in the runtime output layout"
    )
endif()

# Copies pure source files directly into application directory for runtime reflection parsing
add_custom_target(bgl_copy_shader_src ALL
    DEPENDS bgl_compile_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
    COMMENT "Copying raw Slang source assets to runtime output layout"
    VERBATIM
)
