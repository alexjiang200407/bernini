file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE CORE_INTERNAL_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/*.h
)

list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/src/(win32)/.*")

add_library(Core STATIC ${CORE_SOURCES} ${CORE_HEADERS} ${CORE_INTERNAL_HEADERS})


find_package(spdlog CONFIG REQUIRED)

target_link_libraries(
    Core
    PUBLIC
        spdlog::spdlog_header_only
)

target_precompile_headers(Core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/pch.h
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

target_include_directories(Core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(Core PRIVATE ${CORE_SOURCES} ${CORE_HEADERS} ${CORE_INTERNAL_HEADERS})

if (PLATFORM STREQUAL "WINDOWS")
    target_compile_definitions(Core PRIVATE PLATFORM_WINDOWS)

    file(GLOB_RECURSE WIN32_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/*.hpp
    )

    target_sources(Core PRIVATE ${WIN32_SOURCES})

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${WIN32_SOURCES})
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${CORE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/Core" PREFIX "Shared Header Files" FILES ${CORE_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Internal Header Files" FILES ${CORE_INTERNAL_HEADERS})
