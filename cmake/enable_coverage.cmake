# llvm-cov source-based coverage. Per-target so the FetchContent trees (QtNodes, metalcpp)
# stay uninstrumented; see docs/coverage.md.
option(BUILD_COVERAGE "Instrument targets for llvm-cov source-based coverage (clang only)" OFF)

function(enable_coverage)
    if(NOT BUILD_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "BUILD_COVERAGE requires clang; ${CMAKE_CXX_COMPILER_ID} has no source-based coverage.")
    endif()

    if(NOT ARGN)
        message(WARNING "enable_coverage: No targets were passed to the function.")
        return()
    endif()

    foreach(TARGET_NAME ${ARGN})
        if(NOT TARGET ${TARGET_NAME})
            message(FATAL_ERROR "enable_coverage: '${TARGET_NAME}' is not a valid CMake target.")
        endif()

        target_compile_options(${TARGET_NAME} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        # PUBLIC, and a link option even though nothing here links yet: instrumented objects
        # reference ___llvm_profile_runtime, which only a -fprofile-instr-generate link resolves,
        # and the executables consuming these libraries have no enable_coverage call of their own.
        target_link_options(${TARGET_NAME} PUBLIC -fprofile-instr-generate)
    endforeach()
endfunction()
