function(compile_shader)
    # Define the expected arguments for the function
    set(options "")
    
    set(oneValueArgs FILE OUT_DIR TARGET STAGE) 
    
    # ENTRY_POINTS is now treated as an optional multi-value list
    set(multiValueArgs INCLUDES ENTRY_POINTS) 
    cmake_parse_arguments(SHADER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Validate mandatory inputs
    if(NOT SHADER_FILE)
        message(FATAL_ERROR "compile_shader: FILE argument is missing.")
    endif()
    if(NOT SHADER_OUT_DIR)
        message(FATAL_ERROR "compile_shader: OUT_DIR argument is missing.")
    endif()
    if(NOT SHADER_TARGET)
        set(SHADER_TARGET "dxil") 
    endif()

    # Extract filename components
    get_filename_component(SHADER_FILENAME "${SHADER_FILE}" NAME)
    get_filename_component(SHADER_BARE_NAME "${SHADER_FILENAME}" NAME_WE)

    if(NOT SHADER_STAGE)
        if(${SHADER_FILENAME} MATCHES "^VS.*\\.slang$")
            set(SHADER_STAGE "vs_6_6")
        elseif(${SHADER_FILENAME} MATCHES "^PS.*\\.slang$")
            set(SHADER_STAGE "ps_6_6")
        elseif(${SHADER_FILENAME} MATCHES "^GS.*\\.slang$")
            set(SHADER_STAGE "gs_6_6")
        elseif(${SHADER_FILENAME} MATCHES "^MS.*\\.slang$")
            set(SHADER_STAGE "ms_6_6")
        elseif(${SHADER_FILENAME} MATCHES "^AS.*\\.slang$")
            set(SHADER_STAGE "as_6_6")
        elseif(${SHADER_FILENAME} MATCHES "^CS.*\\.slang$")
            set(SHADER_STAGE "cs_6_6")
        else()
            message(FATAL_ERROR "compile_shader: Unable to infer shader stage from filename. Please specify")
        endif()
    endif()
    
    if(SHADER_TARGET STREQUAL "spirv")
        set(EXT ".spv")
    else()
        set(EXT ".dxil")
    endif()

    # Process include directories: map each path to a separate '-I <path>' argument
    set(SLANG_INCLUDE_ARGS "")
    foreach(INC_DIR ${SHADER_INCLUDES})
        list(APPEND SLANG_INCLUDE_ARGS "-I" "${INC_DIR}")
    endforeach()

    # Cache global parent tracking list locally so we can safely append
    set(LOCAL_OUTS ${SLANG_COMPILE_OUTS})

    # FALLBACK LOGIC: If no entry points are provided, default to a list containing just "main"
    if(NOT SHADER_ENTRY_POINTS)
        set(TARGET_ENTRY_POINTS "main")
        set(IS_DEFAULT_ENTRY TRUE)
    else()
        set(TARGET_ENTRY_POINTS ${SHADER_ENTRY_POINTS})
        set(IS_DEFAULT_ENTRY FALSE)
    endif()

    # LOOP THROUGH EACH ENTRY POINT TARGET
    foreach(ENTRY ${TARGET_ENTRY_POINTS})
        
        # Determine unique output name based on whether we are using the fallback default "main"
        if(IS_DEFAULT_ENTRY)
            # Default fallback: No extra underscores or suffixes added
            set(SHADER_OUT_NAME "${SHADER_BARE_NAME}${EXT}")
        else()
            # Custom multi-entry path: Append '_EntryName' suffix
            set(SHADER_OUT_NAME "${SHADER_BARE_NAME}_${ENTRY}${EXT}")
        endif()

        set(OUT_FILE "${SHADER_OUT_DIR}/${SHADER_OUT_NAME}")
        set(DEP_FILE "${OUT_FILE}.d")

        if (IS_DEBUG)
            # -g2: debug info. -D BERNINI_GPU_DEBUG: enables dbg_raise() bodies in
            # shaders; kept in lockstep with the C++/runtime-session define so the
            # feature is fully stripped from Release shaders.
            set(OTHER_FLAGS -g2 -DBERNINI_GPU_DEBUG=1)
        endif()

        # Register the compilation command target
        add_custom_command(
            OUTPUT "${OUT_FILE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
            COMMAND ${SLANG_EXECUTABLE}
                -profile ${SHADER_STAGE}
                -target ${SHADER_TARGET} ${SLANG_INCLUDE_ARGS}
                -entry ${ENTRY}
                -line-directive-mode none
                -depfile "${DEP_FILE}"
                -o "${OUT_FILE}" "${SHADER_FILE}"
                ${OTHER_FLAGS}
            DEPENDS "${SHADER_FILE}"
            DEPFILE "${DEP_FILE}"
            COMMENT "Compiling Slang Entry Point: ${SHADER_FILENAME} [${ENTRY}] -> ${SHADER_OUT_NAME}"
            VERBATIM
        )

        # Track the uniquely generated file inside our lists
        list(APPEND LOCAL_OUTS "${OUT_FILE}")
        
        if(IS_DEFAULT_ENTRY)
            set(${SHADER_BARE_NAME}_OUTPUT "${OUT_FILE}" PARENT_SCOPE)
        else()
            set(${SHADER_BARE_NAME}_${ENTRY}_OUTPUT "${OUT_FILE}" PARENT_SCOPE)
        endif()

    endforeach()

    # Propagate the updated compiled files list straight back to the caller scope
    set(SLANG_COMPILE_OUTS ${LOCAL_OUTS} PARENT_SCOPE)

endfunction()
# The Slang profile a `[shader("<stage>")]` attribute compiles against.
function(slang_stage_profile STAGE OUT_VAR)
    set(STAGE_PROFILES
        vertex        vs_6_6
        pixel         ps_6_6
        geometry      gs_6_6
        mesh          ms_6_6
        amplification as_6_6
        compute       cs_6_6
    )

    list(FIND STAGE_PROFILES "${STAGE}" idx)
    if(idx EQUAL -1)
        message(FATAL_ERROR "slang_stage_profile: no profile for shader stage '${STAGE}'")
    endif()

    math(EXPR idx "${idx} + 1")
    list(GET STAGE_PROFILES ${idx} profile)
    set(${OUT_VAR} "${profile}" PARENT_SCOPE)
endfunction()

# Every entry point a Slang program declares, as a list of `<stage>:<name>` -- read from each
# `[shader("...")]` attribute and the function it annotates. What a program must compile is a fact
# about its source, so the source is what is asked; a build list kept by hand is one a shader can be
# left out of, which is how an unvalidated shader reaches master (see docs/slang_shaders.md).
function(slang_entry_points SHADER_FILE OUT_VAR)
    file(STRINGS "${SHADER_FILE}" LINES)

    set(ENTRIES "")
    set(PENDING_STAGE "")

    foreach(LINE IN LISTS LINES)
        if(LINE MATCHES "^[ \t]*\\[shader\\(\"([a-z]+)\"\\)\\]")
            set(PENDING_STAGE "${CMAKE_MATCH_1}")
        elseif(NOT PENDING_STAGE STREQUAL "")
            # [numthreads], [outputtopology] and blank lines sit between the shader attribute and
            # the function it annotates.
            if(LINE MATCHES "^[ \t]*\\[" OR LINE MATCHES "^[ \t]*$")
                continue()
            endif()

            if(NOT LINE MATCHES "^[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\\(")
                message(FATAL_ERROR
                    "slang_entry_points: ${SHADER_FILE}: [shader(\"${PENDING_STAGE}\")] is not "
                    "followed by a declaration this can read -- '${LINE}'")
            endif()

            list(APPEND ENTRIES "${PENDING_STAGE}:${CMAKE_MATCH_1}")
            set(PENDING_STAGE "")
        endif()
    endforeach()

    set(${OUT_VAR} "${ENTRIES}" PARENT_SCOPE)
endfunction()
