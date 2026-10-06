file(GLOB_RECURSE HEADLESS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE HEADLESS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(headless STATIC ${HEADLESS_SOURCES} ${HEADLESS_HEADERS})

# What the AI viewer and the headless examples share: a device with no window, a model framed on
# its bounds, and a run's pass costs summarised. Kept apart from example_util so nothing here links
# SDL or RmlUi.
target_link_libraries(headless
    PUBLIC
        core
        bgl
        gamelib
        assetlib
)

target_include_directories(headless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

enable_strict_compiler(headless)

target_precompile_headers(headless
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files"
    FILES ${HEADLESS_SOURCES} ${HEADLESS_HEADERS})
