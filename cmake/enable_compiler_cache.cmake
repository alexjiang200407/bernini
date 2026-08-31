# A content-keyed compiler cache in front of the compiler, detected and never required.
#
# ccache keys an object on the preprocessed source, the compiler and the flags, so it returns one
# built before a `git checkout`, a rebase, or a wiped build directory -- none of which ninja can
# survive, because its incremental build is mtimes and a dependency graph inside one build dir.
#
# The sloppiness is not optional here. ccache cannot tell whether a precompiled header used
# __TIME__/__DATE__, and it cannot see the #defines a PCH already resolved, so without
# `pch_defines,time_macros` it declines every translation unit that uses one -- which, in this tree,
# is all of them. clang additionally stamps a PCH with a timestamp that moves on every rebuild, so
# the PCH is compiled with that stamp left out. Both are ccache's documented requirements for PCH
# support; without them the cache is silently useless rather than wrong.
#
# The settings ride in a generated wrapper rather than in the environment, because this build is run
# by scripts/build.py, by ninja directly and by an IDE, and a variable exported by only one of those
# would leave the others missing every time.

option(BERNINI_COMPILER_CACHE "Compile through ccache when it is installed" ON)

function(_bernini_write_cache_wrapper ccache out_var)
    set(cache_dir "${CMAKE_BINARY_DIR}/compiler-cache")
    file(MAKE_DIRECTORY "${cache_dir}")

    # base_dir lets ccache rewrite absolute paths under the checkout into relative ones, which is
    # what gives two worktrees of the same commit a chance of sharing an entry. It is not enough on
    # its own for a debug build -- the working directory reaches the object through DWARF, and
    # hashing it is what keeps a cached object's debug info pointing at the tree it was built from.
    if (WIN32)
        set(wrapper "${cache_dir}/ccache-wrapper.bat")
        file(WRITE "${wrapper}"
            "@echo off\r\n"
            "set CCACHE_SLOPPINESS=pch_defines,time_macros\r\n"
            "set CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}\r\n"
            "\"${ccache}\" %*\r\n")
    else()
        set(wrapper "${cache_dir}/ccache-wrapper.sh")
        file(WRITE "${wrapper}"
            "#!/bin/sh\n"
            "CCACHE_SLOPPINESS=pch_defines,time_macros\n"
            "CCACHE_BASEDIR='${CMAKE_SOURCE_DIR}'\n"
            "export CCACHE_SLOPPINESS CCACHE_BASEDIR\n"
            "exec '${ccache}' \"$@\"\n")
        file(CHMOD "${wrapper}" PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()

    set(${out_var} "${wrapper}" PARENT_SCOPE)
endfunction()

# Call from the top-level CMakeLists **before** any add_subdirectory: a compiler launcher is read
# when a target is created, so one set afterwards reaches nothing.
function(enable_compiler_cache)
    if (NOT BERNINI_COMPILER_CACHE)
        return()
    endif()

    # Visual Studio and Xcode ignore CMAKE_<LANG>_COMPILER_LAUNCHER outright, so say so rather than
    # reporting a cache that is not in the compile line.
    if (CMAKE_GENERATOR MATCHES "Visual Studio|Xcode")
        message(STATUS "Compiler cache: skipped -- ${CMAKE_GENERATOR} ignores compiler launchers")
        return()
    endif()

    # MSVC is refused on the compiler, not on the generator: the Ninja presets drive cl.exe and do
    # honour a launcher, so a generator check alone would let ccache in front of it. ccache's
    # support for MSVC precompiled headers is an open issue with a reported *false hit* -- a wrong
    # object returned rather than a miss -- and every target here carries a PCH, so there is no
    # configuration in this tree where that would be safe. A wrong object is worse than a slow build.
    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        message(STATUS "Compiler cache: skipped -- ccache and MSVC precompiled headers can hit wrongly")
        return()
    endif()

    find_program(BERNINI_CCACHE_PROGRAM ccache)
    if (NOT BERNINI_CCACHE_PROGRAM)
        message(STATUS "Compiler cache: ccache not found -- compiling uncached (`just init` installs it)")
        return()
    endif()

    _bernini_write_cache_wrapper("${BERNINI_CCACHE_PROGRAM}" wrapper)

    set(CMAKE_C_COMPILER_LAUNCHER   "${wrapper}" PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${wrapper}" PARENT_SCOPE)

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Not a warning-suppression or a tuning flag: without it every rebuild of a PCH changes its
        # hash and no TU that uses one can ever hit.
        add_compile_options("SHELL:-Xclang -fno-pch-timestamp")
    endif()

    message(STATUS "Compiler cache: ${BERNINI_CCACHE_PROGRAM}")
endfunction()
