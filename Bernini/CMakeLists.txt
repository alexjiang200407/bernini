include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sourcelist.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/headerlist.cmake)

set(SHADER_FILES
    shaders/VertexShader_Empty.hlsl
)

# Set shader properties BEFORE adding to executable
foreach(SHADER ${SHADER_FILES})
    set_source_files_properties(${SHADER}
        PROPERTIES
            VS_SHADER_TYPE Pixel
            VS_SHADER_MODEL "5.0"
            VS_SHADER_ENTRYPOINT "main"
            VS_SHADER_OBJECT_FILE_NAME "$(OutDir)shaders/%(Filename).cso"
            VS_SHADER_FLAGS "/Od"
            VS_SHADER_ENABLE_DEBUG "true"
    )
endforeach()

# Add executable WITH shaders included from the start
add_executable(Bernini WIN32 ${BERNINI_SOURCES} ${SHADER_FILES})

# Add to filter
source_group("Shaders" FILES ${SHADER_FILES})

target_link_libraries(Bernini PRIVATE Core)