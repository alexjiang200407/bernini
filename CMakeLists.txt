cmake_minimum_required(VERSION 3.20)
project(BerniniEngine)

include(${CMAKE_SOURCE_DIR}/cmake/target_force_include.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/copy_to_target.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(core)
set(FG_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/FrameGraph)
add_subdirectory(extern/NVRHI)
add_subdirectory(gfx)
add_subdirectory(gfxsdk_cxx)

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

get_property(EXAMPLE_TARGETS GLOBAL PROPERTY GLOBAL_EXAMPLE_TARGETS)

if (MSVC)
    foreach(target Core gfx ${EXAMPLE_TARGETS} gfxsdk_cxx)
        target_compile_options(${target} PRIVATE
            /W4          
            /WX               
            /MP          
            /we4265      
            /wd4996      
            /we4244      
            /we4267      
            /we4800      
            /we4305     
            $<$<CONFIG:Release>:/EHs-c-> 
            $<$<NOT:$<CONFIG:Release>>:/EHsc>
        )
    endforeach()
endif()
