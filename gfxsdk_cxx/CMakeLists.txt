file(GLOB_RECURSE GFX_CPP_SDK_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE GFX_CPP_SDK_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gfx/cpp_sdk/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gfx/cpp_sdk/*.h
)

add_library(gfxsdk_cxx STATIC ${GFX_CPP_SDK_SOURCES} ${GFX_CPP_SDK_HEADERS})


target_link_libraries(
    gfxsdk_cxx
    PRIVATE
        Core
        gfx
)

target_precompile_headers(gfxsdk_cxx
    PUBLIC
        ${CMAKE_SOURCE_DIR}/PCH/pch.h
)

target_include_directories(gfxsdk_cxx
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(gfxsdk_cxx PRIVATE ${GFX_CPP_SDK_SOURCES} ${GFX_CPP_SDK_HEADERS})

