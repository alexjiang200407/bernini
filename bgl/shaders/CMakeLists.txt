file(GLOB SLANG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.slang"
)

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

foreach(SHADER ${SLANG_SOURCES})
    get_filename_component(SHADER_BARE_NAME "${SHADER}" NAME_WE)

    compile_shader(
        FILE     "${SHADER}"
        OUT_DIR  "${SHADER_OUT_DIR}"
        TARGET   "dxil"
        INCLUDES ${SLANG_SOURCE_ROOT}
    )

    list(APPEND SLANG_COMPILE_OUTS "${${SHADER_BARE_NAME}_OUTPUT}")
endforeach()

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

    add_custom_target(CopyBGLShaderTests ALL
        DEPENDS CompileShaders
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/src"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/tests"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/tests"
        COMMENT "Copying raw Slang source assets to runtime output layout"
        VERBATIM
    )
endif()

add_custom_target(CompileShaders ALL
    DEPENDS ${SLANG_COMPILE_OUTS}
)

add_custom_target(CopyBGLShaders ALL
    DEPENDS CompileShaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_OUT_DIR}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    COMMENT "Copying compiled DXIL binaries to runtime output layout"
    VERBATIM
)

# Copies pure source files directly into application directory for runtime reflection parsing
add_custom_target(CopyBGLShaderSources ALL
    DEPENDS CompileShaders
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
    COMMENT "Copying raw Slang source assets to runtime output layout"
    VERBATIM
)
