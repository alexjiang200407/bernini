# ForceInclude.cmake
# Utility function to force-include header files in a target
# This is useful when you want headers included without using PCH

#[[
Usage:
    target_force_include(my_target
        PRIVATE
            "${CMAKE_SOURCE_DIR}/common/pch.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/local_pch.h"
    )
    
    target_force_include(my_target PUBLIC "global_header.h")
]]

function(target_force_include TARGET_NAME)
    # Parse arguments: expect visibility (PRIVATE/PUBLIC/INTERFACE) followed by list of headers
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs PRIVATE PUBLIC INTERFACE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Process PRIVATE headers
    if(ARG_PRIVATE)
        foreach(HEADER ${ARG_PRIVATE})
            if(MSVC)
                target_compile_options(${TARGET_NAME} PRIVATE "/FI${HEADER}")
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${TARGET_NAME} PRIVATE "-include" "${HEADER}")
            else()
                message(WARNING "Force include not implemented for this compiler")
            endif()
        endforeach()
    endif()
    
    # Process PUBLIC headers
    if(ARG_PUBLIC)
        foreach(HEADER ${ARG_PUBLIC})
            if(MSVC)
                target_compile_options(${TARGET_NAME} PUBLIC "/FI${HEADER}")
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${TARGET_NAME} PUBLIC "-include" "${HEADER}")
            else()
                message(WARNING "Force include not implemented for this compiler")
            endif()
        endforeach()
    endif()
    
    # Process INTERFACE headers
    if(ARG_INTERFACE)
        foreach(HEADER ${ARG_INTERFACE})
            if(MSVC)
                target_compile_options(${TARGET_NAME} INTERFACE "/FI${HEADER}")
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${TARGET_NAME} INTERFACE "-include" "${HEADER}")
            else()
                message(WARNING "Force include not implemented for this compiler")
            endif()
        endforeach()
    endif()
endfunction()
