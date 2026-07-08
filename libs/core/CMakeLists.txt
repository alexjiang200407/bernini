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

list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/src/(win32)/.*")

add_library(core STATIC ${CORE_SOURCES} ${CORE_HEADERS} ${CORE_INTERNAL_HEADERS})


find_package(spdlog CONFIG REQUIRED)
strip_msvc_only_interface_flags(spdlog::spdlog spdlog::spdlog_header_only)
find_package(cpptrace CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)   # Settings (JSON config) parsing
find_package(glm CONFIG REQUIRED)             # core/glm.h: shared glm config for all libs

target_link_libraries(
    core
    PUBLIC
        spdlog::spdlog_header_only
        glm::glm
    PRIVATE
        cpptrace::cpptrace
        nlohmann_json::nlohmann_json
)

enable_strict_compiler(core)

target_precompile_headers(core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/core/pch.h
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
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

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${WIN32_SOURCES})
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${CORE_SOURCES} ${CORE_INTERNAL_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/core" PREFIX "Shared Header Files" FILES ${CORE_HEADERS})

if (BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)

    file(GLOB_RECURSE CORE_TESTS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )

    add_executable(core_tests ${CORE_TESTS_SOURCES})

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
        PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
    )

    set_target_properties(core_tests PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${CORE_TESTS_SOURCES})

    enable_strict_compiler(core_tests)
endif()
