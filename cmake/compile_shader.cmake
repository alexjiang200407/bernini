function(compile_shader)
    # Define the expected arguments for the function
    set(options "")
    set(oneValueArgs FILE OUT_DIR TARGET)
    set(multiValueArgs INCLUDES) # Allows a list of multiple include paths
    cmake_parse_arguments(SHADER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Validate mandatory inputs
    if(NOT SHADER_FILE)
        message(FATAL_ERROR "compile_shader: FILE argument is missing.")
    endif()
    if(NOT SHADER_OUT_DIR)
        message(FATAL_ERROR "compile_shader: OUT_DIR argument is missing.")
    endif()
    if(NOT SHADER_TARGET)
        set(SHADER_TARGET "dxil") # Default fallback
    endif()

    # Extract filename and determine target file extensions
    get_filename_component(SHADER_FILENAME "${SHADER_FILE}" NAME)
    
    if(SHADER_TARGET STREQUAL "spirv")
        set(EXT ".spv")
    else()
        set(EXT ".dxil")
    endif()
    
    string(REPLACE ".slang" "${EXT}" SHADER_OUT_NAME "${SHADER_FILENAME}")
    
    set(OUT_FILE "${SHADER_OUT_DIR}/${SHADER_OUT_NAME}")
    set(DEP_FILE "${OUT_FILE}.d")

    # Process include directories: map each path to a separate '-I <path>' argument
    set(SLANG_INCLUDE_ARGS "")
    foreach(INC_DIR ${SHADER_INCLUDES})
        list(APPEND SLANG_INCLUDE_ARGS "-I" "${INC_DIR}")
    endforeach()

    # Detect shader stage profiles based on naming convention
    if(${SHADER_FILENAME} MATCHES "^VS.*\\.slang$")
        set(PROFILE "vs_6_6")
    elseif(${SHADER_FILENAME} MATCHES "^PS.*\\.slang$")
        set(PROFILE "ps_6_6")
    elseif(${SHADER_FILENAME} MATCHES "^GS.*\\.slang$")
        set(PROFILE "gs_6_6")
    elseif(${SHADER_FILENAME} MATCHES "^MS.*\\.slang$")
        set(PROFILE "ms_6_6")
    elseif(${SHADER_FILENAME} MATCHES "^AS.*\\.slang$")
        set(PROFILE "as_6_6")
    else()
        set(PROFILE "sm_6_6")
    endif()

    # Register the custom command
    add_custom_command(
        OUTPUT "${OUT_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMAND ${SLANG_EXECUTABLE}
            -profile ${PROFILE}
            -target ${SHADER_TARGET} ${SLANG_INCLUDE_ARGS}
            -entry main
            -line-directive-mode none
            -depfile "${DEP_FILE}"
            -o "${OUT_FILE}" "${SHADER_FILE}"
        DEPENDS "${SHADER_FILE}"
        DEPFILE "${DEP_FILE}"
        COMMENT "Compiling Slang shader ${SHADER_FILENAME} -> ${SHADER_TARGET} [${PROFILE}]"
        VERBATIM
    )

    # Propagate the output file variable up to the caller's scope
    get_filename_component(SHADER_BARE_NAME "${SHADER_FILENAME}" NAME_WE)
    set(${SHADER_BARE_NAME}_OUTPUT "${OUT_FILE}" PARENT_SCOPE)

endfunction()