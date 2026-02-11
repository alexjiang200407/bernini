file(GLOB_RECURSE MATERIAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE MATERIAL_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(materials WIN32
    ${MATERIAL_SOURCES}
    ${MATERIAL_HEADERS}
)

target_link_libraries(materials PRIVATE Core gfx gfxsdk_cxx)

target_precompile_headers(materials
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${MATERIAL_SOURCES}
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Header Files"
    FILES ${MATERIAL_HEADERS}
)


