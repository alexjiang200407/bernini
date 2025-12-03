cmake_minimum_required(VERSION 3.20)
project(BerniniEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(Core)
add_subdirectory(extern/FrameGraph)
add_subdirectory(extern/NVRHI)
add_subdirectory(Gfx)
add_subdirectory(Game)


if (MSVC)
    foreach(target Core gfx Game)
        target_compile_options(${target} PRIVATE
            /W4          
            /WX          
            /EHsc        
            /MP          
            /we4265      
            /wd4996      
            /we4244      
            /we4267      
            /we4800      
            /we4305     
        )
    endforeach()
endif()
