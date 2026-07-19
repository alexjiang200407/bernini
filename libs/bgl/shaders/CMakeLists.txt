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

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Forward_StaticMesh.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "as_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "ASMain"
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Forward_StaticMesh.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ms_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "MSMain"
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Forward_PBR.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ps_6_6"
    ENTRY_POINTS "PSMain"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Forward_PBR_Loose.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ps_6_6"
    ENTRY_POINTS "PSMain"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Forward_Null.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ps_6_6"
    ENTRY_POINTS "PSMain"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Forward_Assert.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ps_6_6"
    ENTRY_POINTS "PSMain"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/FullscreenRect.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ms_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "MSMain"
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/FullscreenRect.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ps_6_6"
    ENTRY_POINTS "PSMain"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Skybox.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ms_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "MSMain"
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/Skybox.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ps_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "PSMain"
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/HistogramInstances.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "cs_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/PrefixSumInstances.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "cs_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/src/CompactInstances.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "cs_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
)

message(STATUS "Discovered Compiled Shaders: ${SLANG_COMPILE_OUTS}")


if (BUILD_TESTS)
    file(GLOB SLANG_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.slang"
    )

    foreach(SHADER ${SLANG_TEST_SOURCES})
        get_filename_component(SHADER_BARE_NAME "${SHADER}" NAME_WE)

        compile_shader(
            FILE     "${SHADER}"
            OUT_DIR  "${SHADER_OUT_DIR}"
            TARGET   "dxil"
            INCLUDES ${SLANG_SOURCE_ROOT}
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
