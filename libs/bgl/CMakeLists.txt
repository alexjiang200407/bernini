add_subdirectory(./idl)

file(GLOB_RECURSE BGL_COMMON_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
list(FILTER BGL_COMMON_SOURCES EXCLUDE REGEX ".*/src/d3d12/.*")

file(GLOB_RECURSE BGL_SHARED_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/bgl/*.hpp
)

file(GLOB_RECURSE BGL_COMMON_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)
list(FILTER BGL_COMMON_HEADERS EXCLUDE REGEX ".*/src/d3d12/.*")


source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${BGL_COMMON_SOURCES} ${BGL_COMMON_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/bgl" PREFIX "Shared Header Files" FILES ${BGL_SHARED_HEADERS})


add_library(bgl_objects OBJECT
	${BGL_COMMON_SOURCES}
	${BGL_COMMON_HEADERS}
)

target_include_directories(bgl_objects
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)


target_compile_definitions(bgl_objects PRIVATE
	BGL_EXPORTS
)

target_precompile_headers(bgl_objects
	PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

find_package(slang CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)  # stb_image_write, for RenderContext's screenshot PNG encode

target_include_directories(bgl_objects PRIVATE ${Stb_INCLUDE_DIR})

target_link_libraries(bgl_objects
	PUBLIC
		core
		assetlib_structs
		slang::slang
		glm::glm
)

if(RENDERER_BACKEND STREQUAL "DX12")
	add_subdirectory(src/d3d12)
endif()

add_dependencies(bgl_objects bgl_idl_generate)
if(RENDERER_BACKEND STREQUAL "DX12")
	add_dependencies(bgl_d3d12 bgl_idl_generate)
endif()

enable_strict_compiler(bgl_objects)

# CreateGraphics is only ever defined inside a backend, and the shader step compiles for the
# backend's IR, so nothing below can be built without one.
if(NOT RENDERER_BACKEND)
	message(STATUS "RENDERER_BACKEND unset - bgl_objects only (no bgl runtime, shaders or tests)")
	return()
endif()

if(RENDERER_BACKEND STREQUAL "DX12")
	add_library(bgl SHARED
		$<TARGET_OBJECTS:bgl_objects>
		$<TARGET_OBJECTS:bgl_d3d12>
		${BGL_SHARED_HEADERS}
	)
else()
	add_library(bgl SHARED
		$<TARGET_OBJECTS:bgl_objects>
		${BGL_SHARED_HEADERS}
	)
endif()

target_include_directories(bgl
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_dependencies(bgl bgl_idl_generate)

target_link_libraries(bgl PUBLIC bgl_objects glm::glm)

enable_strict_compiler(bgl)

target_compile_definitions(bgl PRIVATE
	BGL_EXPORTS
)

target_compile_definitions(bgl_objects PRIVATE
	BGL_EXPORTS
)

if(RENDERER_BACKEND STREQUAL "DX12")
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/shaders)
	add_dependencies(bgl bgl_copy_shaders bgl_copy_shader_src)

	target_compile_definitions(bgl_d3d12 PRIVATE
		BGL_EXPORTS
	)

	target_compile_definitions(bgl PRIVATE RENDERER_BACKEND_DX12)
	target_link_libraries(bgl PRIVATE bgl_d3d12)

	find_package(directx12-agility CONFIG REQUIRED)
	copy_to_target(bgl ITEMS "$<TARGET_FILE:Microsoft::DirectX12-Core>" "$<TARGET_FILE:Microsoft::DirectX12-Layers>")
	enable_strict_compiler(bgl_d3d12)
endif()


if (BUILD_TESTS AND RENDERER_BACKEND STREQUAL "DX12")
    find_package(Catch2 CONFIG REQUIRED)
	find_package(Stb REQUIRED)  # stb_image for golden-image comparison

    file(GLOB_RECURSE BGL_TESTS_SOURCE CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )
    
    add_executable(bgl_tests
        ${BGL_TESTS_SOURCE}
        $<TARGET_OBJECTS:bgl_objects>
    )
    
    target_include_directories(bgl_tests
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
            ${Stb_INCLUDE_DIR}
    )

    target_force_include(bgl_tests
        PRIVATE "${CMAKE_SOURCE_DIR}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
    )
    
    target_link_libraries(
			bgl_tests
		PRIVATE
			bgl_objects
			assetlib
			Catch2::Catch2
			Catch2::Catch2WithMain
			core
	)

	add_dependencies(bgl_tests bgl_copy_shaders bgl_copy_shader_src bgl_copy_shader_tests bgl_idl_generate)

	target_precompile_headers(bgl_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${BGL_TESTS_SOURCE})

    if(RENDERER_BACKEND STREQUAL "DX12")
        target_compile_definitions(bgl_tests PRIVATE RENDERER_BACKEND_DX12)
		target_link_libraries(bgl_tests PRIVATE bgl_d3d12 bgl_d3d12_agility)
		target_sources(bgl_tests PRIVATE $<TARGET_OBJECTS:bgl_d3d12>)
    endif()

	target_compile_definitions(bgl_tests PRIVATE
		BGL_EXPORTS
	)

	set_target_properties(bgl_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	# Deploy the shared asset directory next to the test exe (cwd at run time), so
	# tests resolve maps/meshes as "assets/<name>" and golden refs as
	# "assets/golden/<name>.png". copy_assets (root CMakeLists.txt) stages it once
	# for every consumer.
	add_dependencies(bgl_tests copy_assets)

	enable_strict_compiler(bgl_tests)
endif()

