file(GLOB_RECURSE BGL_TWO_WINDOWS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BGL_TWO_WINDOWS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(bgl_two_windows WIN32
    ${BGL_TWO_WINDOWS_SOURCES}
    ${BGL_TWO_WINDOWS_HEADERS}
)

target_link_libraries(bgl_two_windows PRIVATE core bgl)

if(RENDERER_BACKEND STREQUAL "DX12")
    target_link_libraries(bgl_two_windows PRIVATE bgl_d3d12_agility)
endif()

target_include_directories(bgl_two_windows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../util
)

target_precompile_headers(bgl_two_windows
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${BGL_TWO_WINDOWS_SOURCES} ${BGL_TWO_WINDOWS_HEADERS}
)
