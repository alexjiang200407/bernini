file(GLOB_RECURSE MESH_CREATE_UPDATE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE MESH_CREATE_UPDATE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(static_mesh_crud WIN32
    ${MESH_CREATE_UPDATE_SOURCES}
    ${MESH_CREATE_UPDATE_HEADERS}
)

target_link_libraries(static_mesh_crud PRIVATE Core gfx gfxsdk_cxx)

target_precompile_headers(static_mesh_crud
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${MESH_CREATE_UPDATE_SOURCES}
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Header Files"
    FILES ${MESH_CREATE_UPDATE_HEADERS}
)


