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

target_link_libraries(bgl_objects
	PUBLIC
		Core
		slang::slang
		glm::glm
)

if(RENDERER_BACKEND STREQUAL "DX12")	
	add_subdirectory(src/d3d12)
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

add_dependencies(bgl CopyBGLShaders CopyBGLShaderSources)

target_link_libraries(bgl PUBLIC bgl_objects)

target_compile_definitions(bgl PRIVATE
	BGL_EXPORTS
)

target_compile_definitions(bgl_objects PRIVATE
	BGL_EXPORTS
)

target_compile_definitions(bgl_d3d12 PRIVATE
	BGL_EXPORTS
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/shaders)

if(RENDERER_BACKEND STREQUAL "DX12")	
	target_compile_definitions(bgl PRIVATE RENDERER_BACKEND_DX12)	
	target_link_libraries(bgl PRIVATE bgl_d3d12) 	
	set(D3D_DLLS
        "${CMAKE_SOURCE_DIR}/extern/d3d12_dlls/D3D12"
    )

	copy_to_target(bgl ITEMS ${D3D_DLLS})
endif()
