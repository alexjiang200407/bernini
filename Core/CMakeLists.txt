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

target_precompile_headers(Core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/pch.h
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

target_include_directories(Core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(Core PRIVATE ${CORE_SOURCES} ${CORE_HEADERS})

