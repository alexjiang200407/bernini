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

        # A ';' inside a generator expression is not a list separator, so a piece is rejoined
        # until its `$<` and `>` balance before it is judged.
        set(FILTERED "")
        set(PENDING "")
        foreach(OPT IN LISTS OPTS)
            if(PENDING)
                set(OPT "${PENDING};${OPT}")
            endif()
            string(REGEX MATCHALL "\\$<" OPENS "${OPT}")
            string(REGEX MATCHALL ">" CLOSES "${OPT}")
            list(LENGTH OPENS OPEN_COUNT)
            list(LENGTH CLOSES CLOSE_COUNT)
            if(OPEN_COUNT GREATER CLOSE_COUNT)
                set(PENDING "${OPT}")
                continue()
            endif()
            set(PENDING "")
            if(OPT MATCHES "^/")
                continue()
            endif()
            list(APPEND FILTERED "${OPT}")
        endforeach()

        set_target_properties(${TARGET_NAME} PROPERTIES INTERFACE_COMPILE_OPTIONS "${FILTERED}")
    endforeach()
endfunction()
