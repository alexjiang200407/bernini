# Strip bare MSVC "/"-flags (e.g. spdlog ships /Zc:__cplusplus) from an imported
# target's INTERFACE_COMPILE_OPTIONS when not building with an MSVC-style driver.
# Generator expressions are left untouched.
function(strip_msvc_only_interface_flags)
    if(MSVC)
        return()
    endif()

    foreach(TARGET_NAME ${ARGN})
        if(NOT TARGET ${TARGET_NAME})
            continue()
        endif()

        get_target_property(OPTS ${TARGET_NAME} INTERFACE_COMPILE_OPTIONS)
        if(NOT OPTS)
            continue()
        endif()

        # A list splits a generator expression at every ';' inside it -- spdlog's
        # $<$<CXX_COMPILER_ID:MSVC>:/wd4251;/wd4275> arrives as two elements, the second
        # starting with '/' -- so only a '/'-flag outside every open $< is bare.
        set(FILTERED "")
        set(GENEX_DEPTH 0)
        foreach(OPT ${OPTS})
            if(GENEX_DEPTH EQUAL 0 AND OPT MATCHES "^/")
                continue()
            endif()
            list(APPEND FILTERED "${OPT}")
            string(REGEX MATCHALL "\\$<" OPENS "${OPT}")
            string(REGEX MATCHALL ">" CLOSES "${OPT}")
            list(LENGTH OPENS OPEN_COUNT)
            list(LENGTH CLOSES CLOSE_COUNT)
            math(EXPR GENEX_DEPTH "${GENEX_DEPTH} + ${OPEN_COUNT} - ${CLOSE_COUNT}")
        endforeach()

        set_target_properties(${TARGET_NAME} PROPERTIES INTERFACE_COMPILE_OPTIONS "${FILTERED}")
    endforeach()
endfunction()
