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
find_package(cpptrace CONFIG REQUIRED)

target_link_libraries(
    core
    PUBLIC
        spdlog::spdlog_header_only
    PRIVATE
        cpptrace::cpptrace
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
