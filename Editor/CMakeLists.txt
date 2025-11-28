file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE EDITOR_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(Editor WIN32
    ${EDITOR_SOURCES}
    ${EDITOR_HEADERS}
)

target_link_libraries(Editor PRIVATE Core Renderer)

target_precompile_headers(Editor
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${EDITOR_SOURCES}
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Header Files"
    FILES ${EDITOR_HEADERS}
)


