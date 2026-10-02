# windeployqt copies the Qt runtime next to a binary, and every Qt binary here shares one runtime
# output directory, so two deploying at once race on the same DLLs and one fails to create it. The
# deploy runs inside the link step, so under Ninja the links that deploy share a pool of one.
include_guard(GLOBAL)

set_property(GLOBAL APPEND PROPERTY JOB_POOLS bernini_qt_deploy=1)

function(deploy_qt_runtime TARGET_NAME)
    if(NOT WIN32)
        return()
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND Qt6::windeployqt
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:${TARGET_NAME}>"
        VERBATIM
        COMMENT "windeployqt: copying Qt runtime next to ${TARGET_NAME}"
    )
    set_property(TARGET ${TARGET_NAME} PROPERTY JOB_POOL_LINK bernini_qt_deploy)
endfunction()
