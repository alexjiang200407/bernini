file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/*.h
)

add_library(Core STATIC ${CORE_SOURCES} ${CORE_HEADERS})


find_package(glfw3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

target_link_libraries(
    Core
    PRIVATE
        glfw
    PUBLIC
        spdlog::spdlog_header_only
)

target_precompile_headers(Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/pch.h
)

target_include_directories(Core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${CORE_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/Core
    PREFIX "Header Files"
    FILES ${CORE_HEADERS})

source_group("Precompile Header File"
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/pch.h)
