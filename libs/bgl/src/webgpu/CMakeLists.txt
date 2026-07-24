if(EMSCRIPTEN)
    # A browser build links Emscripten's emdawnwebgpu port rather than building Dawn.
    message(FATAL_ERROR "RENDERER_BACKEND=WEBGPU does not support Emscripten yet; build it natively")
endif()

file(GLOB_RECURSE BGL_WEBGPU_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE BGL_WEBGPU_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

# tests/ builds into bgl_webgpu_tests below, not into the backend itself.
list(FILTER BGL_WEBGPU_SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER BGL_WEBGPU_HEADERS EXCLUDE REGEX ".*/tests/.*")

include(${CMAKE_CURRENT_SOURCE_DIR}/dawn.cmake)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${BGL_WEBGPU_SOURCES} ${BGL_WEBGPU_HEADERS})

add_library(bgl_webgpu OBJECT
    ${BGL_WEBGPU_SOURCES}
    ${BGL_WEBGPU_HEADERS}
)

target_include_directories(bgl_webgpu
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(bgl_webgpu
    PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(bgl_webgpu PRIVATE RENDERER_BACKEND_WEBGPU)

# slang::slang is a header dependency here: the shared bgl PCH (../pch.h) includes <slang.h>.
target_link_libraries(bgl_webgpu
    PUBLIC  core assetlib_structs glm::glm slang::slang dawn::webgpu_dawn
)

enable_strict_compiler(bgl_webgpu)

if (BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)

    file(GLOB_RECURSE BGL_WEBGPU_TESTS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/*.cpp
    )

    add_executable(bgl_webgpu_tests ${BGL_WEBGPU_TESTS_SOURCES})

    target_include_directories(bgl_webgpu_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/..
                ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    )

    target_precompile_headers(bgl_webgpu_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/PCH/pch.h
                ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
                ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
    )

    target_compile_definitions(bgl_webgpu_tests PRIVATE RENDERER_BACKEND_WEBGPU)

    target_link_libraries(bgl_webgpu_tests
        PRIVATE
            bgl_webgpu
            Catch2::Catch2
            Catch2::Catch2WithMain
    )

    set_target_properties(bgl_webgpu_tests PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests" PREFIX "Source Files" FILES ${BGL_WEBGPU_TESTS_SOURCES})

    enable_strict_compiler(bgl_webgpu_tests)
endif()
