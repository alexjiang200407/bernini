# Copies files or directories to a target's output directory after build
# Usage: copy_to_target(<target> ITEMS <item1> <item2> ...)
function(copy_to_target target)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs ITEMS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR "Target ${target} does not exist!")
    endif()

    foreach(item ${ARG_ITEMS})
        if(NOT EXISTS "${item}")
            message(FATAL_ERROR "Item not found: ${item}")
        endif()

        # Get only the name of the file or folder
        get_filename_component(item_name "${item}" NAME)

        if(IS_DIRECTORY "${item}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${item}"
                        "$<TARGET_FILE_DIR:${target}>/${item_name}"
                COMMENT "Copying directory ${item} to ${target} output directory as folder ${item_name}"
            )
        else()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${item}"
                        "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Copying file ${item} to ${target} output directory"
            )
        endif()
    endforeach()
endfunction()

