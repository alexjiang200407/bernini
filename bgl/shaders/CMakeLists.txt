file(GLOB SLANG_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/*.slang
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
    get_filename_component(SHADER_FILENAME "${SHADER}" NAME)
    string(REPLACE ".slang" ".dxil" SHADER_DXIL_NAME "${SHADER_FILENAME}")
    set(OUT_FILE "${SHADER_OUT_DIR}/${SHADER_DXIL_NAME}")

    if(${SHADER_FILENAME} MATCHES "VS.*\\.slang$")
        set(PROFILE "vs_6_6")
    elseif(${SHADER_FILENAME} MATCHES "PS.*\\.slang$")
        set(PROFILE "ps_6_6")
    else()
        set(PROFILE "sm_6_6")
    endif()

    add_custom_command(
        OUTPUT "${OUT_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMAND ${SLANG_EXECUTABLE} -profile ${PROFILE} -target dxil -I "${SLANG_SOURCE_ROOT}" -o "${OUT_FILE}" "${SHADER}"
        DEPENDS "${SHADER}"
        COMMENT "Compiling Slang shader ${SHADER_FILENAME} [${PROFILE}] -> ${OUT_FILE}"
        VERBATIM
    )
    list(APPEND SLANG_COMPILE_OUTS "${OUT_FILE}")
endforeach()

# Copy all .slang source files to shaders/src so reflection can read them at runtime
set(SHADER_SRC_OUT_DIR "${SHADER_OUT_DIR}/src")
foreach(SHADER ${SLANG_SOURCES})
    get_filename_component(SHADER_NAME_EXT ${SHADER} NAME)
    set(DEST_FILE "${SHADER_SRC_OUT_DIR}/${SHADER_NAME_EXT}")
    add_custom_command(
        OUTPUT "${DEST_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_SRC_OUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${SHADER}" "${DEST_FILE}"
        DEPENDS "${SHADER}"
        COMMENT "Copying ${SHADER_NAME_EXT} to ${SHADER_SRC_OUT_DIR}"
        VERBATIM
    )
    list(APPEND SLANG_COMPILE_OUTS "${DEST_FILE}")
endforeach()

add_custom_target(CompileShaders ALL
    DEPENDS ${SLANG_COMPILE_OUTS}
)

if(GENERATOR_IS_MULTI_CONFIG)
    set(CONFIG_SUBDIR "/$<CONFIG>")
else()
    set(CONFIG_SUBDIR "")
endif()


add_custom_target(CopyBGLShaders ALL
    DEPENDS ${SLANG_COMPILE_OUTS}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_OUT_DIR}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders"
    COMMENT "Copying compiled Slang shaders to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders"
)

add_custom_target(CopyBGLShaderSources ALL
    DEPENDS ${SLANG_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/src"
    COMMENT "Copying Slang source files to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}${CONFIG_SUBDIR}/shaders/src"
)
