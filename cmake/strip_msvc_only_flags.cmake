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

        set(FILTERED "")
        foreach(OPT ${OPTS})
            if(OPT MATCHES "^/")
                continue()
            endif()
            list(APPEND FILTERED "${OPT}")
        endforeach()

        set_target_properties(${TARGET_NAME} PROPERTIES INTERFACE_COMPILE_OPTIONS "${FILTERED}")
    endforeach()
endfunction()
