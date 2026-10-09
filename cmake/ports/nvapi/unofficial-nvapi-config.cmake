get_filename_component(_nvapi_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if (NOT TARGET unofficial::nvapi::nvapi)
    add_library(unofficial::nvapi::nvapi STATIC IMPORTED)
    set_target_properties(unofficial::nvapi::nvapi PROPERTIES
        IMPORTED_LOCATION "${_nvapi_root}/lib/nvapi64.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_nvapi_root}/include/nvapi")
endif()

unset(_nvapi_root)
