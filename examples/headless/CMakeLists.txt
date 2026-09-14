file(GLOB_RECURSE EXAMPLE_HEADLESS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE EXAMPLE_HEADLESS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(example_headless STATIC ${EXAMPLE_HEADLESS_SOURCES} ${EXAMPLE_HEADLESS_HEADERS})

# What the headless examples share: a device with no window, a model framed on its bounds, and a
# run's pass costs summarised. Kept apart from example_util so nothing here links SDL or RmlUi.
target_link_libraries(example_headless
    PUBLIC
        core
        bgl_extended
        gamelib
        assetlib
)

target_include_directories(example_headless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

enable_strict_compiler(example_headless)

target_precompile_headers(example_headless
    PUBLIC
        ${BERNINI_ROOT}/PCH/pch.h
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files"
    FILES ${EXAMPLE_HEADLESS_SOURCES} ${EXAMPLE_HEADLESS_HEADERS})
