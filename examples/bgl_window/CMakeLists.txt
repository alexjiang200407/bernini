file(GLOB_RECURSE BGL_WINDOW_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_executable(bgl_window ${BGL_WINDOW_SOURCES})

target_link_libraries(bgl_window PRIVATE core bgl example_util)

target_precompile_headers(bgl_window
    PUBLIC ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${BGL_WINDOW_SOURCES})
