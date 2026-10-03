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
#
# MSVC is the exception, and opt-in. ccache's MSVC precompiled-header support has a reported false
# hit -- a wrong object returned rather than a miss -- so a cache there is only sound if no compile
# it sees uses a PCH. Sources here depend on what their PCHs bring in, so the PCHs cannot simply be
# switched off: BERNINI_MSVC_COMPILER_CACHE instead replaces `target_precompile_headers` with a
# force-include (/FI) of the same headers, in the same order, which is what a PCH is to the
# translation unit without the binary. Every compile ccache sees is then an ordinary one, hashed on
# its preprocessed text. /Zi becomes /Z7 because a /Zi object names a PDB the compiler writes as a
# side effect, which no cache can replay. A developer's build keeps its real PCHs and its PDBs; the
# cache is for builds that compile everything from nothing, which is what CI does on every run.

option(BERNINI_COMPILER_CACHE "Compile through ccache when it is installed" ON)

if (DEFINED ENV{BERNINI_MSVC_COMPILER_CACHE})
    set(_bernini_msvc_cache_default "$ENV{BERNINI_MSVC_COMPILER_CACHE}")
else()
    set(_bernini_msvc_cache_default OFF)
endif()
option(BERNINI_MSVC_COMPILER_CACHE
    "Compile through ccache under MSVC, with precompiled headers off and /Z7 debug info"
    ${_bernini_msvc_cache_default})

# Defining a function named after a command replaces it for every later caller, which is the point:
# the targets say target_precompile_headers and get a force-include instead.
macro(_bernini_emulate_precompiled_headers)
    function(target_precompile_headers target)
        set(scope PRIVATE)
        foreach(arg IN LISTS ARGN)
            if (arg MATCHES "^(PUBLIC|PRIVATE|INTERFACE)$")
                set(scope "${arg}")
            elseif (arg STREQUAL "REUSE_FROM")
                message(FATAL_ERROR "REUSE_FROM needs a real PCH, which BERNINI_MSVC_COMPILER_CACHE removes")
            else()
                target_compile_options(${target} ${scope} "$<$<COMPILE_LANGUAGE:CXX>:/FI${arg}>")
            endif()
        endforeach()
    endfunction()
endmacro()

function(_bernini_write_cache_wrapper ccache sloppiness out_var)
    set(cache_dir "${CMAKE_BINARY_DIR}/compiler-cache")
    file(MAKE_DIRECTORY "${cache_dir}")

    # The build directory is the consumer's when the engine is embedded, and a consumer that calls
    # this function too would write its own wrapper over ours -- one file, one basedir, last write
    # wins, and the loser compiles uncached with nothing on screen to say so. Naming the wrapper
    # after the basedir it carries is what makes two callers two files.
    string(MD5 basedir_key "${BERNINI_ROOT}")
    string(SUBSTRING "${basedir_key}" 0 8 basedir_key)

    # base_dir lets ccache rewrite absolute paths under the checkout into relative ones, which is
    # what gives two worktrees of the same commit a chance of sharing an entry -- and what makes a
    # game compiling the engine into its own build tree a hit rather than a full build. It is not
    # enough on its own for a debug build -- the working directory reaches the object through DWARF,
    # and hashing it is what keeps a cached object's debug info pointing at the tree it was built
    # from.
    if (WIN32)
        set(wrapper "${cache_dir}/ccache-wrapper-${basedir_key}.bat")
        file(WRITE "${wrapper}"
            "@echo off\r\n"
            "set CCACHE_SLOPPINESS=${sloppiness}\r\n"
            "set CCACHE_COMPILERCHECK=content\r\n"
            "set CCACHE_BASEDIR=${BERNINI_ROOT}\r\n"
            "\"${ccache}\" %*\r\n")
    else()
        set(wrapper "${cache_dir}/ccache-wrapper-${basedir_key}.sh")
        file(WRITE "${wrapper}"
            "#!/bin/sh\n"
            "CCACHE_SLOPPINESS=${sloppiness}\n"
            "CCACHE_BASEDIR='${BERNINI_ROOT}'\n"
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

    find_program(BERNINI_CCACHE_PROGRAM ccache)
    if (NOT BERNINI_CCACHE_PROGRAM)
        message(STATUS "Compiler cache: ccache not found -- compiling uncached (`just init` installs it)")
        return()
    endif()

    # MSVC is decided on the compiler, not on the generator: the Ninja presets drive cl.exe and do
    # honour a launcher. Refused unless the build opted in, because every target here carries a PCH
    # and ccache can return a wrong object for one; opting in removes the PCHs rather than the risk.
    set(sloppiness "pch_defines,time_macros")
    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        if (NOT BERNINI_MSVC_COMPILER_CACHE)
            message(STATUS "Compiler cache: skipped -- MSVC precompiled headers can hit wrongly "
                           "(-DBERNINI_MSVC_COMPILER_CACHE=ON builds without them, and caches)")
            return()
        endif()

        set(sloppiness "")
        _bernini_emulate_precompiled_headers()

        foreach(lang C CXX)
            foreach(config DEBUG RELWITHDEBINFO)
                string(REGEX REPLACE "/Z[iI]" "/Z7" flags "${CMAKE_${lang}_FLAGS_${config}}")
                set(CMAKE_${lang}_FLAGS_${config} "${flags}" PARENT_SCOPE)
            endforeach()
        endforeach()

        # ccache refuses a command compiling several files, and /MP is how cl.exe is told it may.
        # Ninja already runs one compile per file in parallel, so /MP adds nothing but the refusal.
        string(REGEX REPLACE " */MP[0-9]*" "" flags "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS "${flags}" PARENT_SCOPE)
    endif()

    _bernini_write_cache_wrapper("${BERNINI_CCACHE_PROGRAM}" "${sloppiness}" wrapper)

    set(CMAKE_C_COMPILER_LAUNCHER   "${wrapper}" PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${wrapper}" PARENT_SCOPE)

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Not a warning-suppression or a tuning flag: without it every rebuild of a PCH changes its
        # hash and no TU that uses one can ever hit.
        add_compile_options("SHELL:-Xclang -fno-pch-timestamp")
    endif()

    message(STATUS "Compiler cache: ${BERNINI_CCACHE_PROGRAM}")
endfunction()
