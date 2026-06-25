file(GLOB_RECURSE BGL_BASE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_BASE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_base WIN32
    ${BGL_BASE_SOURCES}
    ${BGL_BASE_HEADERS}
)

target_link_libraries(bgl_base PRIVATE core bgl)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_base PRIVATE bgl_d3d12_agility)
endif()

target_include_directories(bgl_base PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../util
)

target_precompile_headers(bgl_base
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_BASE_SOURCES}
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Header Files"
    FILES ${BGL_BASE_HEADERS}
)
