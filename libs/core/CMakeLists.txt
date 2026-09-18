file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE CORE_INTERNAL_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/*.h
)

list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/src/(win32|posix|web|process)/.*")

add_library(core STATIC ${CORE_SOURCES} ${CORE_HEADERS} ${CORE_INTERNAL_HEADERS})


find_package(spdlog CONFIG REQUIRED)
strip_msvc_only_interface_flags(spdlog::spdlog spdlog::spdlog_header_only)
find_package(cpptrace CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)   # Settings (JSON config) parsing
find_package(glm CONFIG REQUIRED)             # core/glm.h: shared glm config for all libs
find_package(Tracy CONFIG REQUIRED)

# The state that is wrong when doubled, which core -- static, and linked into every binary -- cannot
# hold. Shared wherever the renderer is, since the renderer is a DLL. See docs/core_process.md.
file(GLOB_RECURSE CORE_PROCESS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/process/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/process/*.h
)

if (RENDERER_BACKEND STREQUAL "NONE")
    add_library(core_process STATIC ${CORE_PROCESS_SOURCES})
else()
    add_library(core_process SHARED ${CORE_PROCESS_SOURCES})
    target_compile_definitions(core_process
        PUBLIC CORE_PROCESS_SHARED
        PRIVATE CORE_PROCESS_EXPORTS)
endif()

target_include_directories(core_process
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# A dependency with state of its own is linked here, and only here. A shared one is once per
# process already; a static one is linked whole, so every symbol a consumer reaches is exported
# from core_process and no binary pulls a copy of its own out of the archive. Its consumers get
# its headers, its definitions and its dependencies, never the archive.
function(link_once_per_process dependency)
    get_target_property(dependencyType ${dependency} TYPE)
    get_target_property(processType core_process TYPE)

    if (dependencyType STREQUAL "SHARED_LIBRARY" OR processType STREQUAL "STATIC_LIBRARY")
        target_link_libraries(core_process PUBLIC ${dependency})
    else()
        target_link_libraries(core_process PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${dependency}>")
        target_include_directories(core_process SYSTEM PUBLIC
            $<TARGET_PROPERTY:${dependency},INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(core_process PUBLIC
            $<TARGET_PROPERTY:${dependency},INTERFACE_COMPILE_DEFINITIONS>)
        target_link_libraries(core_process PUBLIC
            $<TARGET_PROPERTY:${dependency},INTERFACE_LINK_LIBRARIES>)
    endif()
endfunction()

# Compiled rather than header-only: header-only puts spdlog's registry -- the default logger -- in
# every binary that logs.
link_once_per_process(spdlog::spdlog)

# Off is headers-only: TRACY_ENABLE rides on the link interface, so without it every zone macro
# expands to nothing and no call site changes. Declared at the root, set per preset.
if (BERNINI_PROFILING)
    link_once_per_process(Tracy::TracyClient)
else()
    target_include_directories(core_process SYSTEM PUBLIC
        $<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

enable_strict_compiler(core_process)
enable_coverage(core_process)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${CORE_PROCESS_SOURCES})

target_link_libraries(
    core
    PUBLIC
        core_process
        glm::glm
    PRIVATE
        cpptrace::cpptrace
        nlohmann_json::nlohmann_json
)

enable_strict_compiler(core)
enable_coverage(core)

target_precompile_headers(core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/core/pch.h
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

target_include_directories(core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(core PRIVATE ${CORE_SOURCES} ${CORE_HEADERS} ${CORE_INTERNAL_HEADERS})

if (PLATFORM STREQUAL "WINDOWS")
    target_compile_definitions(core PRIVATE PLATFORM_WINDOWS)

    file(GLOB_RECURSE WIN32_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.hpp
    )

    target_sources(core PRIVATE ${WIN32_SOURCES})

    # GetProcessMemoryInfo, for core::process_memory.
    target_link_libraries(core PRIVATE psapi)

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${WIN32_SOURCES})
elseif (PLATFORM STREQUAL "MACOS")
    target_compile_definitions(core PRIVATE PLATFORM_MACOS)

    file(GLOB_RECURSE POSIX_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/posix/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/posix/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/posix/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/posix/*.hpp
    )

    target_sources(core PRIVATE ${POSIX_SOURCES})

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${POSIX_SOURCES})
elseif (PLATFORM STREQUAL "WEB")
    target_compile_definitions(core PRIVATE PLATFORM_WEB)

    file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/web/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/web/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/web/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/web/*.hpp
    )

    target_sources(core PRIVATE ${WEB_SOURCES})

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${WEB_SOURCES})
else()
    message(FATAL_ERROR "PLATFORM must be WINDOWS, MACOS or WEB (got '${PLATFORM}'); core has no platform layer otherwise")
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${CORE_SOURCES} ${CORE_INTERNAL_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/core" PREFIX "Shared Header Files" FILES ${CORE_HEADERS})

if (BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)

    file(GLOB_RECURSE CORE_TESTS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )
    list(FILTER CORE_TESTS_SOURCES EXCLUDE REGEX ".*/tests/fixture/.*")

    # A static core_process is the build with one binary to hold the state, so there is no second
    # one for the [process] cases to load.
    get_target_property(CORE_PROCESS_TYPE core_process TYPE)
    if (NOT CORE_PROCESS_TYPE STREQUAL "SHARED_LIBRARY")
        list(FILTER CORE_TESTS_SOURCES EXCLUDE REGEX ".*/Process_test\\.cpp$")
    endif()

    add_executable(core_tests ${CORE_TESTS_SOURCES})

    # Loaded by core_tests at runtime rather than linked, so it binds to the copy of core its own
    # link gave it: the case a renderer or a plugin is in.
    if (CORE_PROCESS_TYPE STREQUAL "SHARED_LIBRARY")
        add_library(core_process_fixture SHARED ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture/process_fixture.cpp)
        target_link_libraries(core_process_fixture PRIVATE core)
        enable_strict_compiler(core_process_fixture)

        add_dependencies(core_tests core_process_fixture)
        target_compile_definitions(core_tests PRIVATE
            CORE_PROCESS_FIXTURE="$<TARGET_FILE:core_process_fixture>")
    endif()

    target_include_directories(core_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
    )

    target_link_libraries(core_tests
        PRIVATE
            core
            Catch2::Catch2
            Catch2::Catch2WithMain
    )

    target_precompile_headers(core_tests
        PUBLIC  ${BERNINI_ROOT}/PCH/pch.h
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
    )

    set_target_properties(core_tests PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${CORE_TESTS_SOURCES})

    enable_strict_compiler(core_tests)
    enable_coverage(core_tests)
endif()
