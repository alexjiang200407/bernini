file(GLOB_RECURSE BGL_EXTENDED_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
list(FILTER BGL_EXTENDED_SOURCES EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")

# The public surface lives in bgl; these are listed here only so an IDE shows them under the
# renderer that implements them.
file(GLOB_RECURSE BGL_SHARED_HEADERS CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/libs/bgl/include/bgl/*.h
	${CMAKE_SOURCE_DIR}/libs/bgl/include/bgl/*.hpp
)

file(GLOB_RECURSE BGL_EXTENDED_HEADERS CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)
list(FILTER BGL_EXTENDED_HEADERS EXCLUDE REGEX ".*/src/(d3d12|metal)/.*")


source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${BGL_EXTENDED_SOURCES} ${BGL_EXTENDED_HEADERS})
source_group(TREE "${CMAKE_SOURCE_DIR}/libs/bgl/include/bgl" PREFIX "Shared Header Files" FILES ${BGL_SHARED_HEADERS})


add_library(bgl_extended_objects OBJECT
	${BGL_EXTENDED_SOURCES}
	${BGL_EXTENDED_HEADERS}
)

target_include_directories(bgl_extended_objects
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
)


target_compile_definitions(bgl_extended_objects PRIVATE
	BGL_EXPORTS
)

target_precompile_headers(bgl_extended_objects
	PUBLIC  ${CMAKE_SOURCE_DIR}/PCH/pch.h
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)

find_package(slang CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)  # stb_image_write, for RenderContext's screenshot PNG encode

target_include_directories(bgl_extended_objects PRIVATE ${Stb_INCLUDE_DIR})

target_link_libraries(bgl_extended_objects
	PUBLIC
		bgl
		bgl_common
		core
		assetlib_structs
		slang::slang
		glm::glm
)

if(RENDERER_BACKEND STREQUAL "DX12")
	add_subdirectory(src/d3d12)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	add_subdirectory(src/metal)
endif()

add_dependencies(bgl_extended_objects bgl_idl_generate)
if(RENDERER_BACKEND STREQUAL "DX12")
	add_dependencies(bgl_d3d12 bgl_idl_generate)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	add_dependencies(bgl_metal bgl_idl_generate)
endif()

enable_strict_compiler(bgl_extended_objects)
enable_coverage(bgl_extended_objects)

# CreateGraphics is only ever defined inside a backend, and the shader step compiles for the
# backend's IR, so nothing below can be built without one.
if(NOT RENDERER_BACKEND)
	message(STATUS "RENDERER_BACKEND unset - bgl_extended_objects only (no bgl_extended runtime, shaders or tests)")
	return()
endif()

if(RENDERER_BACKEND STREQUAL "DX12")
	add_library(bgl_extended SHARED
		$<TARGET_OBJECTS:bgl_extended_objects>
		$<TARGET_OBJECTS:bgl_d3d12>
		${BGL_SHARED_HEADERS}
	)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	add_library(bgl_extended SHARED
		$<TARGET_OBJECTS:bgl_extended_objects>
		$<TARGET_OBJECTS:bgl_metal>
		${BGL_SHARED_HEADERS}
	)
else()
	add_library(bgl_extended SHARED
		$<TARGET_OBJECTS:bgl_extended_objects>
		${BGL_SHARED_HEADERS}
	)
endif()

target_include_directories(bgl_extended
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_dependencies(bgl_extended bgl_idl_generate)

target_link_libraries(bgl_extended PUBLIC bgl bgl_extended_objects glm::glm)

enable_strict_compiler(bgl_extended)

target_compile_definitions(bgl_extended PRIVATE
	BGL_EXPORTS
)

target_compile_definitions(bgl_extended_objects PRIVATE
	BGL_EXPORTS
)

# The build-time Slang step emits DXIL; only the D3D12 backend consumes it. The Metal backend
# compiles its shaders at runtime (MSL via newLibraryWithSource) and needs no build-time step.
if(RENDERER_BACKEND STREQUAL "DX12")
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/shaders)
	add_dependencies(bgl_copy_shader_src bgl_common_copy_shaders)
	add_dependencies(bgl_extended bgl_copy_shaders bgl_copy_shader_src)

	target_compile_definitions(bgl_d3d12 PRIVATE
		BGL_EXPORTS
	)

	target_compile_definitions(bgl_extended PRIVATE RENDERER_BACKEND_DX12)
	target_link_libraries(bgl_extended PRIVATE bgl_d3d12)

	find_package(directx12-agility CONFIG REQUIRED)
	copy_to_target(bgl_extended ITEMS "$<TARGET_FILE:Microsoft::DirectX12-Core>" "$<TARGET_FILE:Microsoft::DirectX12-Layers>")
	enable_strict_compiler(bgl_d3d12)
	enable_coverage(bgl_d3d12)
elseif(RENDERER_BACKEND STREQUAL "METAL")
	target_compile_definitions(bgl_metal PRIVATE BGL_EXPORTS)
	# bgl_extended_objects too, not just the backend: DescriptorHandle's alignment is per-backend, and
	# bgl_extended_objects is what compiles the scene code that puts one in a GPU struct.
	target_compile_definitions(bgl_extended PRIVATE RENDERER_BACKEND_METAL)
	target_compile_definitions(bgl_extended_objects PRIVATE RENDERER_BACKEND_METAL)
	# bgl_extended embeds bgl_metal's objects, so it links the frameworks bgl_metal's PRIVATE deps do not carry.
	target_link_libraries(bgl_extended PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore")
	enable_strict_compiler(bgl_metal)
	enable_coverage(bgl_metal)

	# The Metal backend compiles Slang at runtime, so the raw .slang sources must sit beside the
	# executable -- Device_metal's session resolves modules from "./shaders/src", and a build that
	# staged none aborts on the first program-cache miss with "cannot open file". Hung off bgl_extended, the
	# way the D3D12 build hangs bgl_copy_shader_src off it, so every consumer of a device gets them.
	#
	# A target of its own rather than a POST_BUILD step: POST_BUILD only runs when the library
	# relinks, so editing a .slang alone would leave the staged copy stale.
	file(GLOB_RECURSE BGL_METAL_SHADER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.slang
	)

	set(BGL_METAL_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/metal_shaders.stamp")
	add_custom_command(
		OUTPUT  "${BGL_METAL_SHADER_STAMP}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/src"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/src"
		COMMAND ${CMAKE_COMMAND} -E touch "${BGL_METAL_SHADER_STAMP}"
		DEPENDS ${BGL_METAL_SHADER_SOURCES}
		COMMENT "Staging Slang sources for runtime Metal compilation"
		VERBATIM
	)
	add_custom_target(bgl_metal_copy_shaders ALL DEPENDS "${BGL_METAL_SHADER_STAMP}")
	add_dependencies(bgl_metal_copy_shaders bgl_common_copy_shaders)
	add_dependencies(bgl_extended bgl_metal_copy_shaders)
endif()

# Slang reaches DXC through GetProcAddress -- dxcompiler.dll to emit DXIL, dxil.dll to sign it --
# so nothing imports either, and vcpkg's applocal deployment stages a DLL by walking the import
# table. The Slang release carries neither. Unsigned bytecode loads only in Developer Mode, so
# both are copied by hand, for the same reason as the Agility SDK above.
#
# dxil.dll is taken from beside dxcompiler.dll rather than through Microsoft::DXIL, whose config
# sets IMPORTED_LOCATION_RELASE -- so that target resolves to nothing in a Release build. Reading
# both from one directory also keeps the compiler and its validator a matched pair, which they
# have to be: a validator older than the compiler rejects the container.
if(WIN32)
	find_package(directx-dxc CONFIG REQUIRED)
	copy_to_target(bgl_extended ITEMS
		"$<TARGET_FILE:Microsoft::DirectXShaderCompiler>"
		"$<TARGET_FILE_DIR:Microsoft::DirectXShaderCompiler>/dxil.dll"
	)
endif()


# The suite's sources are backend-agnostic (RHI interfaces + CreateGraphics), so it builds on either
# backend from one glob. What differs is the backend objects it embeds.
if (BUILD_TESTS AND RENDERER_BACKEND STREQUAL "DX12")
    find_package(Catch2 CONFIG REQUIRED)
	find_package(Stb REQUIRED)  # stb_image for golden-image comparison

    file(GLOB_RECURSE BGL_TESTS_SOURCE CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )
    
    add_executable(bgl_extended_tests
        ${BGL_TESTS_SOURCE}
        $<TARGET_OBJECTS:bgl_extended_objects>
    )
    
    target_include_directories(bgl_extended_tests
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
            ${Stb_INCLUDE_DIR}
    )

    target_force_include(bgl_extended_tests
        PRIVATE "${CMAKE_SOURCE_DIR}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
    )
    
    target_link_libraries(
			bgl_extended_tests
		PRIVATE
			bgl_extended_objects
			assetlib
			Catch2::Catch2
			Catch2::Catch2WithMain
			core
	)

	# bgl_extended is a dependency for what it stages beside the executable, not for linking: the suite links
	# the backend's objects directly, so building it alone would otherwise leave dxcompiler.dll and
	# dxil.dll unstaged. Depending on bgl_extended rather than copying again keeps one writer per file --
	# two POST_BUILD steps copying the same DLL into the same directory can run at once.
	add_dependencies(bgl_extended_tests bgl_extended bgl_copy_shaders bgl_copy_shader_src bgl_copy_shader_tests bgl_idl_generate)

	target_precompile_headers(bgl_extended_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/src" PREFIX "Source Files" FILES ${BGL_TESTS_SOURCE})

    if(RENDERER_BACKEND STREQUAL "DX12")
        target_compile_definitions(bgl_extended_tests PRIVATE RENDERER_BACKEND_DX12)
		target_link_libraries(bgl_extended_tests PRIVATE bgl_d3d12 bgl_d3d12_agility)
		target_sources(bgl_extended_tests PRIVATE $<TARGET_OBJECTS:bgl_d3d12>)
    endif()

	target_compile_definitions(bgl_extended_tests PRIVATE
		BGL_EXPORTS
	)

	set_target_properties(bgl_extended_tests PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
	)

	# Deploy the shared asset directory next to the test exe (cwd at run time), so
	# tests resolve maps/meshes as "assets/Data/<name>" and golden refs as
	# "assets/golden/<name>.png" -- the goldens are test fixtures, not project assets, so they sit
	# beside the project rather than inside it. copy_assets (root CMakeLists.txt) stages it once
	# for every consumer.
	add_dependencies(bgl_extended_tests copy_assets)

	# Compiles shaders at run time, so it needs the DLLs bgl_extended stages -- see the DXC block above.
	if(WIN32)
		add_dependencies(bgl_extended_tests bgl_extended)
	endif()

	enable_strict_compiler(bgl_extended_tests)
	enable_coverage(bgl_extended_tests)
endif()

if (BUILD_TESTS AND RENDERER_BACKEND STREQUAL "METAL")
	find_package(Catch2 CONFIG REQUIRED)
	find_package(Stb REQUIRED)  # stb_image for golden-image comparison

	file(GLOB_RECURSE BGL_TESTS_SOURCE CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
	)

	add_executable(bgl_extended_tests
		${BGL_TESTS_SOURCE}
		$<TARGET_OBJECTS:bgl_extended_objects>
		$<TARGET_OBJECTS:bgl_metal>
	)

	target_include_directories(bgl_extended_tests
		PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/tests
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/src
			# The backend's own root: its headers include each other unprefixed, so a Metal-only
			# case cannot reach one without it.
			${CMAKE_CURRENT_SOURCE_DIR}/src/metal
			${CMAKE_CURRENT_SOURCE_DIR}/tests/src
			${Stb_INCLUDE_DIR}
	)

	target_include_directories(bgl_extended_tests SYSTEM PRIVATE ${BGL_METALCPP_INCLUDE_DIR})

	target_force_include(bgl_extended_tests
		PRIVATE "${CMAKE_SOURCE_DIR}/PCH/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
	)

	# Embeds bgl_metal's objects, so it links the frameworks bgl_metal's PRIVATE deps do not carry.
	target_link_libraries(bgl_extended_tests
		PRIVATE
			bgl_extended_objects
			bgl_metal
			assetlib
			core
			Catch2::Catch2
			Catch2::Catch2WithMain
			"-framework Metal" "-framework Foundation" "-framework QuartzCore"
	)

	add_dependencies(bgl_extended_tests bgl_idl_generate copy_assets)

	# Only the test shaders: the src tree beside them is staged by bgl_metal_copy_shaders, which the
	# suite depends on as well because it links the backend's objects rather than bgl_extended. One writer per
	# directory -- two targets copying into the same one can run at once.
	file(GLOB_RECURSE BGL_METAL_TEST_SHADER_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests/*.slang
	)

	set(BGL_METAL_TEST_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/metal_test_shaders.stamp")
	add_custom_command(
		OUTPUT  "${BGL_METAL_TEST_SHADER_STAMP}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/tests"
		COMMAND ${CMAKE_COMMAND} -E touch "${BGL_METAL_TEST_SHADER_STAMP}"
		DEPENDS ${BGL_METAL_TEST_SHADER_SOURCES}
		COMMENT "Staging Slang test shaders for runtime Metal compilation"
		VERBATIM
	)
	add_custom_target(bgl_metal_copy_test_shaders DEPENDS "${BGL_METAL_TEST_SHADER_STAMP}")
	add_dependencies(bgl_extended_tests bgl_metal_copy_shaders bgl_metal_copy_test_shaders)

	target_precompile_headers(bgl_extended_tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/pch.h
	)

	target_compile_definitions(bgl_extended_tests PRIVATE RENDERER_BACKEND_METAL BGL_EXPORTS)

	enable_strict_compiler(bgl_extended_tests)
	enable_coverage(bgl_extended_tests)
endif()
